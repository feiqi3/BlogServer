#include "test_support.h"
#include "Async/FJobGraph.h"
#include "Async/FJobPool.h"
#include "FListener.h"
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <variant>

namespace {
using namespace Fei::Async;
using namespace std::chrono_literals;
using FeiTest::await;
using FeiTest::check;
using FeiTest::LoopRunner;

struct JobCompletion { Result<std::any> result; std::thread::id thread; };

struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool open = false;
    void release() { { std::lock_guard lock(mutex); open = true; } cv.notify_all(); }
    void wait() { std::unique_lock lock(mutex); cv.wait(lock, [&] { return open; }); }
};
struct ReleaseGate {
    std::shared_ptr<Gate> gate;
    ~ReleaseGate() { gate->release(); }
};

class ExampleJob : public Job {
public:
    std::thread::id worker;
    std::promise<JobCompletion> finished;
    std::any Run(CancellationToken) override { worker = std::this_thread::get_id(); return 42; }
    void OnFinish(const Result<std::any>& result) override {
        finished.set_value({result, std::this_thread::get_id()});
    }
};

void testJobThreadsAndOrigin() {
    LoopRunner source;
    LoopRunner target;
    JobPool pool({2, 16});
    bool rejected = false;
    try { pool.submit([](CancellationToken) { return std::any(1); }, [](auto) {}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "A non-Loop caller must specify its completion Loop");

    auto job = std::make_shared<ExampleJob>();
    auto future = job->finished.get_future();
    auto handle = source.run([&] { return pool.submit(job); });
    auto completed = await(future);
    check(completed.result.ok() && std::any_cast<int>(*completed.result.value) == 42, "Job result survives worker-to-Loop transfer");
    check(job->worker != source.threadId() && job->worker != std::this_thread::get_id(), "Run must use worker thread");
    check(completed.thread == source.threadId(), "Implicit origin captures submitting Loop");
    check(handle.done(), "Job handle marks terminal result");

    std::promise<JobCompletion> explicitFinished;
    auto explicitFuture = explicitFinished.get_future();
    source.run([&] {
        pool.submit([](CancellationToken) { return std::any(7); }, [&](auto result) {
            explicitFinished.set_value({std::move(result), std::this_thread::get_id()});
        }, &target.loop());
    });
    auto explicitResult = await(explicitFuture);
    check(explicitResult.thread == target.threadId(), "Explicit completion Loop overrides submitting Loop");
    pool.shutdown(); source.drain(); target.drain();
    check(pool.pending() == 0 && pool.pendingCompletions() == 0, "Job pool drains work and notifications");
}

void testBoundedQueueCancellationAndShutdown() {
    LoopRunner runner;
    JobPool pool({1, 1});
    auto gate = std::make_shared<Gate>();
    ReleaseGate releaseOnExit{gate};
    std::promise<void> started;
    auto startedFuture = started.get_future();
    std::promise<JobCompletion> firstDone;
    auto firstFuture = firstDone.get_future();
    std::atomic_int firstNotifications{0};
    auto first = pool.submit([&](CancellationToken) {
        started.set_value(); gate->wait(); return std::any(1);
    }, [&](auto result) {
        if (++firstNotifications == 1) firstDone.set_value({std::move(result), std::this_thread::get_id()});
    }, &runner.loop());
    await(startedFuture);

    std::atomic_int queuedExecuted{0};
    std::promise<JobCompletion> queuedDone;
    auto queuedFuture = queuedDone.get_future();
    auto queued = pool.submit([&](CancellationToken) { ++queuedExecuted; return std::any(2); }, [&](auto result) {
        queuedDone.set_value({std::move(result), std::this_thread::get_id()});
    }, &runner.loop());

    std::promise<JobCompletion> rejectedDone;
    auto rejectedFuture = rejectedDone.get_future();
    pool.submit([](CancellationToken) { return std::any(3); }, [&](auto result) {
        rejectedDone.set_value({std::move(result), std::this_thread::get_id()});
    }, &runner.loop());
    auto rejection = await(rejectedFuture);
    check(rejection.result.error.code == ErrorCode::QueueFull, "Full worker queue rejects asynchronously");
    check(rejection.thread == runner.threadId(), "Queue rejection uses target Loop");

    queued.cancel(); queued.cancel();
    auto cancelledQueued = await(queuedFuture);
    check(cancelledQueued.result.error.code == ErrorCode::Cancelled && queuedExecuted == 0,
          "Queued cancellation must not execute Run");

    first.cancel(); first.cancel(); runner.drain();
    check(firstFuture.wait_for(0ms) != std::future_status::ready, "Running cancellation cannot call OnFinish before Run exits");
    gate->release();
    auto cancelledRunning = await(firstFuture);
    check(cancelledRunning.result.error.code == ErrorCode::Cancelled, "Running cancellation is reported when Run exits");
    pool.shutdown(); runner.drain();
    check(firstNotifications == 1, "Repeated cancellation produces one notification");

    std::promise<JobCompletion> stoppedDone;
    auto stoppedFuture = stoppedDone.get_future();
    pool.submit([](CancellationToken) { return std::any(4); }, [&](auto result) {
        stoppedDone.set_value({std::move(result), std::this_thread::get_id()});
    }, &runner.loop());
    check(await(stoppedFuture).result.error.code == ErrorCode::Stopped, "Submission after shutdown reports Stopped");
    runner.drain();
}

void testWorkerAndCompletionExceptions() {
    LoopRunner runner;
    JobPool pool({1, 8});
    std::promise<JobCompletion> thrownDone;
    auto thrownFuture = thrownDone.get_future();
    pool.submit([](CancellationToken) -> std::any { throw std::runtime_error("test worker throw"); }, [&](auto result) {
        thrownDone.set_value({std::move(result), std::this_thread::get_id()});
    }, &runner.loop());
    check(await(thrownFuture).result.error.code == ErrorCode::UserException, "Worker exception becomes a failure result");

    std::promise<void> throwingHandlerReached;
    auto throwingFuture = throwingHandlerReached.get_future();
    pool.submit([](CancellationToken) { return std::any(2); }, [&](auto) {
        throwingHandlerReached.set_value(); throw std::runtime_error("test completion throw");
    }, &runner.loop());
    await(throwingFuture);
    std::promise<void> healthy;
    auto healthyFuture = healthy.get_future();
    pool.submit([](CancellationToken) { return std::any(3); }, [&](auto result) {
        if (result.ok()) healthy.set_value();
    }, &runner.loop());
    await(healthyFuture);
    pool.shutdown(); runner.drain();
    check(pool.pendingCompletions() == 0, "Throwing handlers cannot leak notification accounting");
}

void testJobCooperativeShutdown() {
    LoopRunner runner;
    JobPool pool({1, 2});
    std::promise<void> started;
    auto startedFuture = started.get_future();
    std::promise<JobCompletion> completed;
    auto future = completed.get_future();
    pool.submit([&](CancellationToken token) {
        started.set_value();
        while (!token.cancelled()) std::this_thread::yield();
        return std::any(0);
    }, [&](auto result) { completed.set_value({std::move(result), std::this_thread::get_id()}); }, &runner.loop());
    await(startedFuture);
    pool.shutdown();
    check(await(future).result.error.code == ErrorCode::Cancelled, "Shutdown signals cancellation to running Job");
    runner.drain();
}

void testGraphDeferredExecutionAndThreads() {
    LoopRunner runner;
    JobPool pool({1, 8});
    std::atomic_int entered{0};
    std::thread::id transformThread, workerThread, continuationThread;
    std::promise<int> finished;
    auto future = finished.get_future();
    auto graph = JobGraph::begin(runner.loop())
        .then([&] { ++entered; transformThread = std::this_thread::get_id(); return 6; })
        .thenJob(pool, [&](const int& input, CancellationToken) {
            workerThread = std::this_thread::get_id(); return input * 7;
        })
        .then([&](const int& value) { continuationThread = std::this_thread::get_id(); return value + 1; })
        .onFinish([&](const int& value) { finished.set_value(value); });
    runner.drain();
    check(entered == 0, "Constructing graph must not execute a step");
    auto handle = graph.dispatch(2s);
    check(await(future) == 43, "Graph passes typed values between Loop and worker nodes");
    check(transformThread == runner.threadId() && continuationThread == runner.threadId(), "Graph continuations use bound Loop");
    check(workerThread != runner.threadId(), "Graph Job runs off Loop");
    check(handle.done(), "Graph handle becomes terminal");
    pool.shutdown(); runner.drain();
}

void testGraphFailureAndHandlerExceptions() {
    LoopRunner runner;
    JobPool pool({1, 8});
    std::atomic_int later{0}, successes{0}, failures{0};
    std::promise<ErrorCode> failed;
    auto failureFuture = failed.get_future();
    JobGraph::begin(runner.loop())
        .thenJob(pool, []() -> int { throw std::runtime_error("stage failure"); })
        .then([&](const int&) { ++later; return 1; })
        .onFinish([&](const int&) { ++successes; })
        .onError([&](const Error& error) { ++failures; failed.set_value(error.code); throw std::runtime_error("handler failure"); })
        .dispatch(1s);
    check(await(failureFuture) == ErrorCode::UserException, "Job error reaches final onError");
    runner.drain();
    check(later == 0 && successes == 0 && failures == 1, "Failure halts later nodes and notifies once");

    std::promise<void> successHandlerReached;
    auto successFuture = successHandlerReached.get_future();
    JobGraph::begin(runner.loop()).then([] { return 1; })
        .onFinish([&](const int&) { successHandlerReached.set_value(); throw std::runtime_error("success handler throw"); })
        .onError([&](const Error&) { ++failures; }).dispatch();
    await(successFuture); runner.drain();
    check(failures == 1, "Throwing final success handler does not issue a second final failure");

    std::promise<ErrorCode> loopThrow;
    auto loopFuture = loopThrow.get_future();
    JobGraph::begin(runner.loop()).then([]() -> int { throw std::runtime_error("Loop stage failure"); })
        .onError([&](const Error& error) { loopThrow.set_value(error.code); }).dispatch();
    check(await(loopFuture) == ErrorCode::UserException, "Loop step exception is contained");
    pool.shutdown(); runner.drain();
}

void testGraphDuplicateCompletionAndRepeatDispatch() {
    LoopRunner runner;
    std::atomic_int finals{0}, continued{0};
    std::promise<int> finished;
    auto future = finished.get_future();
    JobGraph::begin(runner.loop())
        .thenAsync<int>([](const std::monostate&, auto done) {
            done(Result<int>::success(9));
            done(Result<int>::failure({ErrorCode::Io, "duplicate failure"}));
            done(Result<int>::success(99));
            return OperationHandle{};
        })
        .then([&](const int& value) { ++continued; return value + 1; })
        .onFinish([&](const int& value) { ++finals; finished.set_value(value); }).dispatch();
    check(await(future) == 10, "First asynchronous completion wins");
    runner.drain();
    check(finals == 1 && continued == 1, "Duplicate completion cannot start duplicate continuations");

    std::promise<void> twice;
    auto twiceFuture = twice.get_future();
    std::atomic_int count{0}, sum{0};
    auto repeat = JobGraph::begin(runner.loop()).then([] { return 17; }).onFinish([&](const int& value) {
        sum += value; if (++count == 2) twice.set_value();
    });
    auto first = repeat.dispatch();
    auto second = repeat.dispatch();
    await(twiceFuture); runner.drain();
    check(sum == 34 && first.id() != second.id() && first.done() && second.done(), "Repeated dispatch starts independent runs");
}

void testGraphErrorHandlerSurvivesAppendAndMutableDispatchIsolation() {
    LoopRunner runner;
    JobPool pool({1, 8});
    std::promise<ErrorCode> failed;
    auto failureFuture = failed.get_future();
    // Attach the error policy before the chain changes result type twice.
    JobGraph::begin(runner.loop())
        .onError([&](const Error& error) { failed.set_value(error.code); })
        .then([] { return std::string("input"); })
        .thenJob(pool, [](const std::string&) -> int { throw std::runtime_error("type-changing stage failed"); })
        .dispatch();
    check(await(failureFuture) == ErrorCode::UserException, "Appending nodes must preserve an existing onError handler");

    std::promise<void> finished;
    auto finishedFuture = finished.get_future();
    std::atomic_int count{0}, sum{0};
    auto graph = JobGraph::begin(runner.loop())
        .then([value = 0]() mutable { return ++value; })
        .thenJob(pool, [value = 0](const int& previous) mutable { return previous + ++value; })
        .onFinish([&](const int& value) {
            sum += value;
            if (++count == 2) finished.set_value();
        });
    graph.dispatch(); graph.dispatch();
    await(finishedFuture);
    pool.shutdown(); runner.drain();
    check(sum == 4 && count == 2, "Independent dispatches must not share mutable lambda state");
    bool rejected = false;
    try { graph.then([](const int& value) { return value; }); }
    catch (const std::logic_error&) { rejected = true; }
    check(rejected, "Appending after typed onFinish must reject an invalid builder order instead of silently dropping it");
}

void testGraphCancelBeforeStart() {
    LoopRunner runner;
    auto gate = std::make_shared<Gate>();
    ReleaseGate releaseOnExit{gate};
    std::promise<void> blocked;
    auto blockedFuture = blocked.get_future();
    runner.loop().AddTask([&] { blocked.set_value(); gate->wait(); });
    await(blockedFuture);
    std::atomic_int ran{0}, errors{0};
    std::promise<ErrorCode> cancelled;
    auto future = cancelled.get_future();
    auto handle = JobGraph::begin(runner.loop()).then([&] { ++ran; return 1; })
        .onError([&](const Error& error) { if (++errors == 1) cancelled.set_value(error.code); }).dispatch();
    handle.cancel(); handle.cancel();
    gate->release();
    check(await(future) == ErrorCode::Cancelled, "Cancel before dispatch task runs reports Cancelled");
    runner.drain();
    check(ran == 0 && errors == 1, "Cancel-before-start prevents the first node and duplicate notifications");
}

void testGraphCancellationLifetimeAndLateResult() {
    LoopRunner runner;
    std::function<void(Result<int>)> late;
    std::promise<void> started, failed;
    auto startedFuture = started.get_future();
    auto failedFuture = failed.get_future();
    std::atomic_int next{0}, errors{0}, cancelCalls{0};
    auto held = std::make_shared<int>(42);
    std::weak_ptr<int> weak = held;
    OperationHandle handle;
    {
        auto graph = JobGraph::begin(runner.loop())
            .thenAsync<int>([&, owner = held](const std::monostate&, auto done) {
                late = std::move(done); started.set_value();
                return OperationHandle(nextOperationId(), [&] { ++cancelCalls; }, std::make_shared<std::atomic_bool>(false));
            })
            .then([&](const int& value) { ++next; return value; })
            .onError([&](const Error&) { if (++errors == 1) failed.set_value(); });
        handle = graph.dispatch();
    }
    held.reset(); await(startedFuture);
    check(!weak.expired(), "Running graph retains its step state");
    handle.cancel(); handle.cancel();
    await(failedFuture); runner.drain();
    check(cancelCalls == 1 && errors == 1, "Graph cancellation propagates to active child once");
    check(weak.expired(), "Finished graph releases steps even when foreign source retains a late completion");
    late(Result<int>::success(7)); runner.drain();
    check(next == 0 && errors == 1, "Late completion after cancellation is ignored");
}

void testGraphTimeoutAndBetweenStageCancellation() {
    LoopRunner runner;
    std::atomic_int next{0}, cancelledChild{0};
    std::promise<ErrorCode> failed;
    auto future = failed.get_future();
    auto timed = JobGraph::begin(runner.loop()).thenAsync<int>([&](const std::monostate&, auto) {
        return OperationHandle(nextOperationId(), [&] { ++cancelledChild; }, std::make_shared<std::atomic_bool>(false));
    }).then([&](const int&) { ++next; return 1; }).onError([&](const Error& error) {
        failed.set_value(error.code);
    }).dispatch(30ms);
    check(await(future) == ErrorCode::Timeout && timed.done(), "Graph total deadline terminates hung asynchronous stage");
    runner.drain();
    check(next == 0 && cancelledChild == 1, "Deadline cancels child and prevents downstream execution");

    std::promise<void> firstCompleted;
    auto firstFuture = firstCompleted.get_future();
    std::promise<ErrorCode> cancelled;
    auto cancelFuture = cancelled.get_future();
    OperationHandle handle;
    auto gate = std::make_shared<Gate>();
    ReleaseGate releaseOnExit{gate};
    handle = JobGraph::begin(runner.loop()).then([&] {
        firstCompleted.set_value(); gate->wait(); return 3;
    }).then([&](const int& value) { ++next; return value; }).onError([&](const Error& error) {
        cancelled.set_value(error.code);
    }).dispatch();
    await(firstFuture); handle.cancel(); gate->release();
    check(await(cancelFuture) == ErrorCode::Cancelled, "Cancellation between nodes interrupts the chain");
    runner.drain();
    check(next == 0, "No next node executes after recorded cancellation");

    // A long Loop transform can postpone the timer callback. The runtime must
    // still check steady-clock deadline before starting the next stage.
    std::promise<ErrorCode> busyTimeout;
    auto busyFuture = busyTimeout.get_future();
    JobGraph::begin(runner.loop()).then([] { std::this_thread::sleep_for(50ms); return 1; })
        .then([&](const int& value) { ++next; return value; })
        .onError([&](const Error& error) { busyTimeout.set_value(error.code); }).dispatch(20ms);
    check(await(busyFuture) == ErrorCode::Timeout && next == 0, "Delayed timer cannot let an expired graph start a later stage");
}

class FastListener final : public Fei::FListener {
public:
    void listen(Fei::uint32, std::vector<Fei::FEventPtr>&) override { std::this_thread::yield(); }
    void addEvent(Fei::FEvent*) override {}
    void removeEvent(Fei::FEvent*) override {}
    void updateEvent(Fei::FEvent*) override {}
};
class FastLoop {
public:
    Fei::FEventLoop loop{std::make_unique<FastListener>()};
    std::thread thread{[this] { loop.Loop(); }};
    ~FastLoop() { loop.Quit(); thread.join(); }
};

void testLongGraphWithoutRecursionAndValidation() {
    FastLoop runner;
    std::promise<int> finished;
    auto future = finished.get_future();
    auto chain = JobGraph::begin(runner.loop).then([] { return 1; });
    for (int i = 1; i < 4096; ++i) chain = chain.then([](const int& value) { return value + 1; });
    bool limit = false;
    try { chain.then([](const int& value) { return value; }); }
    catch (const std::length_error&) { limit = true; }
    check(limit && chain.size() == 4096, "Graph enforces explicit node limit");
    auto handle = chain.onFinish([&](const int& value) { finished.set_value(value); }).dispatch(10s);
    check(await(future) == 4096 && handle.done(), "4096 sequential steps complete without recursive stack growth");
    bool invalidTimeout = false;
    try { JobGraph::begin(runner.loop).dispatch(-1ms); }
    catch (const std::invalid_argument&) { invalidTimeout = true; }
    check(invalidTimeout, "Negative graph timeout is rejected");

    std::promise<Result<int>> hugeTimeoutFinished;
    auto hugeTimeoutFuture = hugeTimeoutFinished.get_future();
    auto hugeTimeoutHandle = JobGraph::begin(runner.loop).then([] { return 23; })
        .onFinish([&](const int& value) { hugeTimeoutFinished.set_value(Result<int>::success(value)); })
        .onError([&](const Error& error) { hugeTimeoutFinished.set_value(Result<int>::failure(error)); })
        .dispatch(std::chrono::milliseconds::max());
    auto hugeTimeoutResult = await(hugeTimeoutFuture);
    check(hugeTimeoutResult.ok() && *hugeTimeoutResult.value == 23 && hugeTimeoutHandle.done(),
          "Maximum positive graph timeout saturates safely and permits a short graph to succeed");
}
} // namespace

void runJobGraphTests() {
    testJobThreadsAndOrigin();
    testBoundedQueueCancellationAndShutdown();
    testWorkerAndCompletionExceptions();
    testJobCooperativeShutdown();
    testGraphDeferredExecutionAndThreads();
    testGraphFailureAndHandlerExceptions();
    testGraphDuplicateCompletionAndRepeatDispatch();
    testGraphErrorHandlerSurvivesAppendAndMutableDispatchIsolation();
    testGraphCancelBeforeStart();
    testGraphCancellationLifetimeAndLateResult();
    testGraphTimeoutAndBetweenStageCancellation();
    testLongGraphWithoutRecursionAndValidation();
}
