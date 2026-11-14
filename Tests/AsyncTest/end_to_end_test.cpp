#include "test_support.h"
#include "Async/FJobGraph.h"
#include "Http/FHttpClient.h"
#include <atomic>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace {
using namespace Fei;
using namespace Fei::Async;
using namespace Fei::Http;
using namespace FeiTest;
using namespace std::chrono_literals;

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) throw std::runtime_error(std::string("Fixture variable required: ") + name);
    return value;
}

void drainClient(FHttpClient& client) {
    auto done = std::make_shared<std::promise<void>>();
    auto future = done->get_future();
    client.shutdown([done] { done->set_value(); });
    await(future);
}

struct State {
    std::promise<Result<ClientResponse>> result;
    std::atomic<int> starts{0}, jobs{0}, completions{0};
    std::atomic<bool> wrongThread{false};
};
}

void runEndToEndTests() {
    const auto base = environment("FEILIB_TEST_HTTP_URL");
    const auto tls = environment("FEILIB_TEST_HTTPS_URL");
    const auto ca = environment("FEILIB_TEST_CA_FILE");
    LoopRunner runner;
    ClientOptions options;
    options.caFile = ca;
    FHttpClient client(runner.loop(), options);
    JobPool workers({2, 16});

    // Mixed HTTP + HTTPS workflow and typed worker result. Construction is inert.
    {
        auto state = std::make_shared<State>();
        auto future = state->result.get_future();
        auto owner = runner.threadId();
        ClientRequest first;
        first.url = base + "/value";
        auto graph = JobGraph::begin(runner.loop())
            .then([state] { ++state->starts; })
            .thenHttp(client, first)
            .thenJob(workers, [state, owner](const ClientResponse& response, CancellationToken token) {
                if (std::this_thread::get_id() == owner) state->wrongThread = true;
                ++state->jobs;
                if (token.cancelled() || response.status != 200) throw std::runtime_error("unexpected initial response");
                return std::stoi(response.body) * 2;
            })
            .thenHttp(client, [state, owner, tls](const int& value) {
                if (std::this_thread::get_id() != owner) state->wrongThread = true;
                ClientRequest request;
                request.url = tls + "/echo";
                request.method = "POST";
                request.body = std::to_string(value);
                return request;
            })
            .onFinish([state, owner](const ClientResponse& response) {
                if (std::this_thread::get_id() != owner) state->wrongThread = true;
                if (++state->completions == 1) state->result.set_value(Result<ClientResponse>::success(response));
            })
            .onError([state](const Error& error) {
                if (++state->completions == 1) state->result.set_value(Result<ClientResponse>::failure(error));
            });
        runner.drain();
        check(state->starts == 0 && state->jobs == 0, "Graph construction must not dispatch work");
        auto handle = graph.dispatch(5s);
        auto result = await(future);
        runner.drain();
        check(result.ok() && result.value->status == 200 && result.value->body == "42", "HTTP -> Job -> HTTPS result propagation failed");
        check(handle.done() && state->starts == 1 && state->jobs == 1 && state->completions == 1,
              "Graph stages and terminal notification must each run once");
        check(!state->wrongThread, "Worker and continuation thread affinity failed");
    }

    // Application-level HTTP failure is explicitly rejected by a short transform.
    {
        auto state = std::make_shared<State>();
        auto future = state->result.get_future();
        ClientRequest request;
        request.url = base + "/status/404";
        auto handle = JobGraph::begin(runner.loop()).thenHttp(client, request)
            .then([](const ClientResponse& response) {
                if (response.status != 200) throw std::runtime_error("application rejected HTTP status");
                return response.body;
            })
            .thenJob(workers, [state](const std::string&) { ++state->jobs; return 7; })
            .onFinish([state](const int&) { ++state->completions; state->result.set_value(Result<ClientResponse>::success({})); })
            .onError([state](const Error& error) { ++state->completions; state->result.set_value(Result<ClientResponse>::failure(error)); })
            .dispatch(5s);
        auto result = await(future);
        check(!result.ok() && result.error.code == ErrorCode::UserException && state->jobs == 0,
              "A failed business step must stop dependent worker jobs");
        check(handle.done() && state->completions == 1, "Failure must complete graph once");
    }

    // Overall graph timeout cancels the active network request and skips dependent work.
    {
        auto state = std::make_shared<State>();
        auto future = state->result.get_future();
        ClientRequest request;
        request.url = base + "/slow?ms=300";
        auto handle = JobGraph::begin(runner.loop()).thenHttp(client, request)
            .thenJob(workers, [state](const ClientResponse&) { ++state->jobs; return 1; })
            .onFinish([state](const int&) { ++state->completions; state->result.set_value(Result<ClientResponse>::success({})); })
            .onError([state](const Error& error) { ++state->completions; state->result.set_value(Result<ClientResponse>::failure(error)); })
            .dispatch(40ms);
        auto result = await(future);
        handle.cancel();
        runner.drain();
        check(!result.ok() && result.error.code == ErrorCode::Timeout && state->jobs == 0,
              "Graph deadline must cancel HTTP and skip later stages");
        check(state->completions == 1, "Late cancellation must not complete a timed out graph twice");
    }

    workers.shutdown();
    runner.drain();
    check(workers.pending() == 0 && workers.pendingCompletions() == 0, "Worker completions must drain before Loop exit");
    drainClient(client);
    runner.drain();
}
