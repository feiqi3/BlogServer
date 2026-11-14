#include "test_support.h"
#include "Http/FHttpClient.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
using Fei::Async::ErrorCode;
using Fei::Async::OperationHandle;
using Fei::Async::Result;
using Fei::Http::ClientOptions;
using Fei::Http::ClientRequest;
using Fei::Http::ClientResponse;
using Fei::Http::FHttpClient;
using FeiTest::check;
using HttpResult = Result<ClientResponse>;
struct Received { HttpResult result; std::thread::id thread; };
struct Call { OperationHandle handle; std::future<Received> future; };

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    check(value && *value, std::string("Missing loopback fixture variable: ") + name);
    return value;
}

// Declared in this order so the client is drained before its Loop is stopped,
// including when a test assertion throws.
struct ClientCase {
    FeiTest::LoopRunner runner;
    FHttpClient client;
    bool closed = false;
    explicit ClientCase(ClientOptions options = {}) : client(runner.loop(), std::move(options)) {}
    ~ClientCase() {
        try { close(); }
        catch (const std::exception& error) { std::cerr << "Client test cleanup failed: " << error.what() << '\n'; }
    }
    Call start(ClientRequest request) {
        auto promise = std::make_shared<std::promise<Received>>();
        auto future = promise->get_future();
        auto handle = client.submit(std::move(request), [promise](HttpResult result) {
            promise->set_value({std::move(result), std::this_thread::get_id()});
        });
        return {std::move(handle), std::move(future)};
    }
    HttpResult wait(Call& call) {
        auto received = FeiTest::await(call.future);
        check(received.thread == runner.threadId(), "HTTP completion ran on the wrong thread");
        check(call.handle.done(), "HTTP operation was not marked done before completion");
        return std::move(received.result);
    }
    HttpResult get(ClientRequest request) { auto call = start(std::move(request)); return wait(call); }
    HttpResult get(const std::string& url) { ClientRequest request; request.url = url; return get(std::move(request)); }
    void close() {
        if (closed) return;
        auto promise = std::make_shared<std::promise<void>>();
        auto future = promise->get_future();
        client.shutdown([promise] { promise->set_value(); });
        FeiTest::await(future);
        runner.drain();
        closed = true;
    }
};

void expectBody(HttpResult result, const std::string& body, const char* description) {
    check(result.ok(), std::string(description) + " failed, code=" + std::to_string(static_cast<int>(result.error.code)) +
          " " + result.error.message);
    check(result.value->body == body, std::string(description) + " body mismatch");
}

void basics(const std::string& http) {
    ClientCase test;
    expectBody(test.get(http + "/value"), "21", "GET");
    expectBody(test.get(http + "/split"), "hello", "Fragmented response");
    expectBody(test.get(http + "/chunked"), "hello", "Chunked response");
    expectBody(test.get(http + "/binary"), std::string("a\0b\xff" "c", 5), "Binary response");
    expectBody(test.get(http + "/eof"), "eof-body", "EOF-delimited response");
    auto missing = test.get(http + "/status/404");
    check(missing.ok() && missing.value->status == 404, "HTTP error status was lost or confused with transport failure");
    auto redirect = test.get(http + "/redirect");
    check(redirect.ok() && redirect.value->status == 302 && redirect.value->header("location") == "/value",
          "Redirect should be returned without implicit follow-up");
    ClientRequest head;
    head.url = http + "/value";
    head.method = "HEAD";
    auto noBody = test.get(std::move(head));
    check(noBody.ok() && noBody.value->body.empty(), "HEAD was not completed without body");
    ClientRequest post;
    post.url = http + "/echo";
    post.method = "POST";
    post.body.assign(1024 * 1024, 'x');
    post.body[9] = '\0';
    post.body[100] = static_cast<char>(0xff);
    const auto expected = post.body;
    expectBody(test.get(std::move(post)), expected, "Large binary POST / partial writes");
    auto truncated = test.get(http + "/truncated");
    check(!truncated.ok() && truncated.error.code == ErrorCode::Protocol, "Truncated response was accepted");
    ClientRequest limited;
    limited.url = http + "/large";
    limited.maxBodyBytes = 1024;
    auto oversized = test.get(std::move(limited));
    check(!oversized.ok() && oversized.error.code == ErrorCode::LimitExceeded, "Body limit was not enforced");
    ClientRequest invalid;
    invalid.url = http + "/value";
    invalid.headers.push_back({"X-Test", "safe\r\nInjected: header"});
    auto injected = test.get(std::move(invalid));
    check(!injected.ok() && injected.error.code == ErrorCode::InvalidArgument, "Header injection was accepted");
    auto tabbed = test.get(http + "/value\tinjected");
    check(!tabbed.ok() && tabbed.error.code == ErrorCode::InvalidArgument, "Whitespace in request target was accepted");
    ClientRequest badTimeout; badTimeout.url = http + "/value"; badTimeout.timeout = 0ms;
    auto zeroTimeout = test.get(badTimeout);
    check(!zeroTimeout.ok() && zeroTimeout.error.code == ErrorCode::InvalidArgument, "Nonpositive timeout was not rejected");
    badTimeout.timeout = std::chrono::milliseconds::max();
    expectBody(test.get(badTimeout), "21", "Huge timeout must saturate without duration overflow");
    auto refused = test.get("http://127.0.0.1:1/value");
    check(!refused.ok() && refused.error.code == ErrorCode::Connect, "Failed TCP connection was not reported");
    expectBody(test.get(http + "/value"), "21", "Client after errors");

    auto returned = std::make_shared<std::atomic_bool>(false);
    auto asynchronous = std::make_shared<std::promise<bool>>();
    auto future = asynchronous->get_future();
    test.runner.run([&] {
        ClientRequest bad; bad.url = "invalid-url";
        test.client.submit(std::move(bad), [returned, asynchronous](HttpResult) {
            asynchronous->set_value(returned->load());
        });
        returned->store(true);
    });
    check(FeiTest::await(future), "Rejected request callback ran inline inside submit");
}

void reuse(const std::string& http) {
    ClientOptions options;
    options.maxConcurrent = options.maxPerOrigin = 1;
    ClientCase test(options);
    auto first = test.get(http + "/connection-id");
    auto second = test.get(http + "/connection-id");
    check(first.ok() && second.ok() && first.value->body == second.value->body, "HTTP keepalive connection was not reused");
    expectBody(test.get(http + "/close"), "closed", "Explicit connection close");
    auto third = test.get(http + "/connection-id");
    check(third.ok() && third.value->body != first.value->body, "Connection: close socket was reused");
    test.close();

    options.maxIdleConnections = 0;
    ClientCase unpooled(options);
    first = unpooled.get(http + "/connection-id");
    second = unpooled.get(http + "/connection-id");
    check(first.ok() && second.ok() && first.value->body != second.value->body, "maxIdleConnections=0 was ignored");
}

void capacityAndDeadlines(const std::string& http) {
    ClientOptions options;
    options.maxConcurrent = options.maxPerOrigin = 1;
    options.maxQueued = 1;
    ClientCase test(options);
    ClientRequest slow; slow.url = http + "/slow?ms=250";
    auto running = test.start(slow);
    test.runner.drain();
    auto queued = test.start(ClientRequest{http + "/value"});
    auto rejected = test.start(ClientRequest{http + "/value"});
    auto rejectedResult = test.wait(rejected);
    check(!rejectedResult.ok() && rejectedResult.error.code == ErrorCode::QueueFull, "Queue capacity did not reject excess submission");
    queued.handle.cancel();
    queued.handle.cancel();
    auto cancelled = test.wait(queued);
    check(!cancelled.ok() && cancelled.error.code == ErrorCode::Cancelled, "Queued cancellation failed");
    expectBody(test.wait(running), "slow", "Running request after queued cancel");

    running = test.start(slow);
    test.runner.drain();
    ClientRequest impatient; impatient.url = http + "/value"; impatient.timeout = 35ms;
    auto pendingDeadline = test.start(impatient);
    auto expired = test.wait(pendingDeadline);
    check(!expired.ok() && expired.error.code == ErrorCode::Timeout, "Deadline did not include time in queue");
    expectBody(test.wait(running), "slow", "Running request after queued deadline");

    ClientRequest timeout; timeout.url = http + "/slow?ms=180"; timeout.timeout = 30ms;
    auto deadline = test.get(timeout);
    check(!deadline.ok() && deadline.error.code == ErrorCode::Timeout, "Active request timeout failed");
    expectBody(test.get(http + "/value"), "21", "Late timed-out response must not be routed to next request");

    auto activeCancel = test.start(slow);
    test.runner.drain();
    std::this_thread::sleep_for(30ms);
    activeCancel.handle.cancel();
    auto activeResult = test.wait(activeCancel);
    check(!activeResult.ok() && activeResult.error.code == ErrorCode::Cancelled, "Active cancellation failed");
    expectBody(test.get(http + "/value"), "21", "Client after active cancellation");
}

void concurrencyAndHeartbeat(const std::string& http) {
    ClientOptions options;
    options.maxConcurrent = 4;
    options.maxPerOrigin = 1;
    ClientCase test(options);
    const auto start = std::chrono::steady_clock::now();
    std::vector<Call> calls;
    for (int i = 0; i < 3; ++i) calls.push_back(test.start(ClientRequest{http + "/slow?ms=110"}));
    for (auto& call : calls) expectBody(test.wait(call), "slow", "Per-origin request");
    check(std::chrono::steady_clock::now() - start >= 300ms, "Per-origin concurrency cap was not respected");

    auto beats = std::make_shared<std::atomic_int>(0);
    auto running = std::make_shared<std::atomic_bool>(true);
    auto heartbeat = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak = heartbeat;
    *heartbeat = [weak, beats, running, &loop = test.runner.loop()] {
        if (!running->load()) return;
        ++*beats;
        if (auto tick = weak.lock()) loop.RunAfter(5, *tick);
    };
    test.runner.loop().AddTask(*heartbeat);
    expectBody(test.get(http + "/slow?ms=350"), "slow", "Slow remote request");
    running->store(false);
    test.runner.drain();
    check(beats->load() >= 12, "Waiting for remote response blocked the shared EventLoop");
}

void cancellationRaces(const std::string& http) {
    ClientOptions options;
    options.maxConcurrent = 8;
    options.maxPerOrigin = 8;
    options.maxQueued = 256;
    ClientCase test(options);
    constexpr int count = 100;
    auto completed = std::make_shared<std::atomic_int>(0);
    auto badThread = std::make_shared<std::atomic_bool>(false);
    auto badResult = std::make_shared<std::atomic_bool>(false);
    auto duplicates = std::make_shared<std::atomic_int>(0);
    auto seen = std::make_shared<std::vector<int>>(count, 0); // only owner Loop touches entries
    auto all = std::make_shared<std::promise<void>>();
    auto future = all->get_future();
    std::vector<OperationHandle> handles;
    for (int i = 0; i < count; ++i) {
        handles.push_back(test.client.submit(ClientRequest{http + "/value"},
            [=, owner = test.runner.threadId()](HttpResult result) {
                if (std::this_thread::get_id() != owner) badThread->store(true);
                if (++(*seen)[i] != 1) ++*duplicates;
                if (!result.ok() && result.error.code != ErrorCode::Cancelled) badResult->store(true);
                if (++*completed == count) all->set_value();
            }));
    }
    std::thread cancel([handles] {
        for (std::size_t i = 0; i < handles.size(); i += 2) { handles[i].cancel(); handles[i].cancel(); }
    });
    cancel.join();
    FeiTest::await(future);
    std::this_thread::sleep_for(50ms);
    test.runner.drain();
    check(completed->load() == count && duplicates->load() == 0, "Cancellation race delivered duplicate/missing completions");
    check(!badThread->load() && !badResult->load(), "Cancellation race wrong thread or unexpected result");
    for (const auto& handle : handles) check(handle.done(), "Cancelled/completed request left pending");
    expectBody(test.get(http + "/value"), "21", "Client after 100 cancellation races");
}

void tls(const std::string& https, const std::string& ca) {
    ClientOptions options; options.caFile = ca;
    ClientCase trusted(options);
    expectBody(trusted.get(https + "/value"), "21", "Trusted TLS");
    ClientRequest post; post.url = https + "/echo"; post.method = "POST"; post.body.assign(512 * 1024, 't');
    auto body = post.body;
    expectBody(trusted.get(std::move(post)), body, "TLS partial write/read");
    auto first = trusted.get(https + "/connection-id");
    auto second = trusted.get(https + "/connection-id");
    check(first.ok() && second.ok() && first.value->body == second.value->body, "TLS keepalive failed");
    auto wrongHost = https;
    auto name = wrongHost.find("localhost");
    check(name != std::string::npos, "TLS fixture needs localhost-only certificate");
    wrongHost.replace(name, 9, "127.0.0.1");
    auto mismatch = trusted.get(wrongHost + "/value");
    check(!mismatch.ok() && mismatch.error.code == ErrorCode::Tls, "TLS hostname mismatch was accepted");
    ClientCase untrusted;
    auto unknown = untrusted.get(https + "/value");
    check(!unknown.ok() && unknown.error.code == ErrorCode::Tls, "Untrusted TLS certificate was accepted");
}

void shutdown(const std::string& http) {
    ClientOptions options; options.maxConcurrent = options.maxPerOrigin = 1;
    ClientCase test(options);
    std::vector<Call> calls;
    for (int i = 0; i < 5; ++i) calls.push_back(test.start(ClientRequest{http + "/slow?ms=600"}));
    test.runner.drain();
    test.close();
    for (auto& call : calls) {
        check(call.future.wait_for(0ms) == std::future_status::ready, "shutdown drained before request completion callbacks");
        auto result = test.wait(call);
        check(!result.ok() && result.error.code == ErrorCode::Stopped, "Shutdown did not stop every admitted request");
    }
    auto stopped = test.get(http + "/value");
    check(!stopped.ok() && stopped.error.code == ErrorCode::Stopped, "Submission after shutdown was not rejected");

    ClientCase callbackFailure;
    auto notified = std::make_shared<std::promise<void>>();
    auto future = notified->get_future();
    callbackFailure.client.submit(ClientRequest{http + "/value"}, [notified](HttpResult) {
        notified->set_value();
        throw std::runtime_error("intentional user callback failure");
    });
    FeiTest::await(future);
    expectBody(callbackFailure.get(http + "/value"), "21", "Throwing callback must not terminate Loop");
    callbackFailure.close();

    // A rejected submission is already queued when shutdown's Loop task runs.
    // onDrained must follow its completion, not merely see zero admitted work.
    ClientCase rejectionDrain;
    auto entered = std::make_shared<std::promise<void>>();
    auto enteredFuture = entered->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto released = release->get_future().share();
    rejectionDrain.runner.loop().AddTask([entered, released] {
        entered->set_value();
        released.wait(); // deterministic test gate, never production client work
    });
    FeiTest::await(enteredFuture);
    auto rejectionCount = std::make_shared<std::atomic_int>(0);
    auto drained = std::make_shared<std::promise<int>>();
    auto drainedFuture = drained->get_future();
    rejectionDrain.client.shutdown([rejectionCount, drained] { drained->set_value(rejectionCount->load()); });
    rejectionDrain.client.submit(ClientRequest{http + "/value"}, [rejectionCount](HttpResult result) {
        if (!result.ok() && result.error.code == ErrorCode::Stopped) ++*rejectionCount;
    });
    release->set_value();
    check(FeiTest::await(drainedFuture) == 1, "shutdown drained before queued rejection callback");
    rejectionDrain.close();

    // Stress destruction at the exact lifetime boundary: DNS has been admitted,
    // shutdown races its completion, then the facade is destroyed in onDrained.
    // Resolver threads must already be joined before that callback can run.
    FeiTest::LoopRunner owner;
    for (int i = 0; i < 100; ++i) {
        ClientOptions tiny;
        tiny.maxConcurrent = tiny.maxPerOrigin = tiny.resolverThreads = 1;
        auto facade = std::make_shared<std::unique_ptr<FHttpClient>>(
            std::make_unique<FHttpClient>(owner.loop(), tiny));
        auto completions = std::make_shared<std::atomic_int>(0);
        (*facade)->submit(ClientRequest{http + "/value"}, [completions](HttpResult) { ++*completions; });
        owner.drain(); // admission ran; native resolution may still be unwinding
        auto destroyed = std::make_shared<std::promise<int>>();
        auto destroyedFuture = destroyed->get_future();
        owner.run([facade, destroyed, completions] {
            (*facade)->shutdown([facade, destroyed, completions] {
                facade->reset();
                destroyed->set_value(completions->load());
            });
        });
        check(FeiTest::await(destroyedFuture) == 1, "Facade destruction raced unfinished completion");
        owner.drain();
    }

    // Destroy the facade before its asynchronous stop task can run. The Loop is
    // deliberately gated so admission has happened and DNS completion is still
    // pending on the Loop, independently of resolver thread scheduling.
    for (int i = 0; i < 20; ++i) {
        ClientOptions tiny;
        tiny.maxConcurrent = tiny.maxPerOrigin = tiny.resolverThreads = 1;
        auto facade = std::make_unique<FHttpClient>(owner.loop(), tiny);
        auto completions = std::make_shared<std::atomic_int>(0);
        facade->submit(ClientRequest{http + "/value"}, [completions](HttpResult) { ++*completions; });
        auto enteredGate = std::make_shared<std::promise<void>>();
        auto gateFuture = enteredGate->get_future();
        auto releaseGate = std::make_shared<std::promise<void>>();
        auto gate = releaseGate->get_future().share();
        owner.loop().AddTask([enteredGate, gate] { enteredGate->set_value(); gate.wait(); });
        FeiTest::await(gateFuture);
        auto drainedEarly = std::make_shared<std::promise<int>>();
        auto drainedEarlyFuture = drainedEarly->get_future();
        facade->shutdown([drainedEarly, completions] { drainedEarly->set_value(completions->load()); });
        facade.reset();
        releaseGate->set_value();
        check(FeiTest::await(drainedEarlyFuture) == 1, "Early facade destruction lost shutdown callback");
        owner.drain();
    }
}
} // namespace

void runHttpClientTests() {
    const auto http = environment("FEILIB_TEST_HTTP_URL");
    const auto https = environment("FEILIB_TEST_HTTPS_URL");
    const auto ca = environment("FEILIB_TEST_CA_FILE");
    std::cout << "HTTP: basics" << std::endl; basics(http);
    std::cout << "HTTP: reuse" << std::endl; reuse(http);
    std::cout << "HTTP: capacity and deadlines" << std::endl; capacityAndDeadlines(http);
    std::cout << "HTTP: concurrency and heartbeat" << std::endl; concurrencyAndHeartbeat(http);
    std::cout << "HTTP: cancellation races" << std::endl; cancellationRaces(http);
    std::cout << "HTTP: TLS" << std::endl; tls(https, ca);
    std::cout << "HTTP: shutdown" << std::endl; shutdown(http);
}
