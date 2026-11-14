#include "Async/FJobGraph.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <memory>

namespace Fei::Async::detail {
namespace {
struct GraphRun : std::enable_shared_from_this<GraphRun> {
    FEventLoop& loop;
    std::vector<GraphStep> steps;
    GraphDone finish;
    std::shared_ptr<std::atomic_bool> done = std::make_shared<std::atomic_bool>(false);
    std::atomic_bool cancelRequested{false};
    std::shared_ptr<GraphRun> keepAlive;
    OperationHandle active;
    std::any value = std::monostate{};
    std::size_t next = 0;
    std::uint64_t generation = 0;
    TimerID timer = 0;
    bool hasDeadline = false;
    std::chrono::steady_clock::time_point deadline;

    GraphRun(FEventLoop& loop, std::vector<GraphStep> steps, GraphDone finish)
        : loop(loop), steps(std::move(steps)), finish(std::move(finish)) {}

    bool expired() const { return hasDeadline && std::chrono::steady_clock::now() >= deadline; }

    void complete(GraphResult result) {
        if (done->exchange(true)) return;
        ++generation;
        if (timer) { loop.CancelTimer(timer); timer = 0; }
        if (!result.ok()) { try { active.cancel(); } catch (...) {} }
        active = {};
        auto self = shared_from_this();
        // Every terminal path is dispatched, never called inline in a parser,
        // worker completion or cancel caller.
        loop.AddTask([self, result = std::move(result)]() mutable {
            try { if (self->finish) self->finish(std::move(result)); }
            catch (...) { /* Final handler exceptions are contained. */ }
            self->finish = {};
            self->steps.clear();
        });
        keepAlive.reset();
    }

    void checkDeadline() {
        if (done->load()) return;
        if (expired()) { complete(GraphResult::failure({ErrorCode::Timeout, "JobGraph deadline exceeded."})); return; }
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        // FEventLoop::RunAfter currently uses system_clock. Do not convert a
        // saturated steady-clock deadline into one enormous wall-clock delay:
        // that conversion/addition could overflow even though our deadline fits.
        // Recheck long deadlines in bounded intervals instead.
        constexpr std::int64_t maximumTimerIntervalMs = 60'000;
        auto interval = std::clamp<std::int64_t>(left.count(), 1, maximumTimerIntervalMs);
        std::weak_ptr<GraphRun> weak = shared_from_this();
        timer = loop.RunAfter(static_cast<uint64>(interval), [weak] {
            if (auto self = weak.lock()) { self->timer = 0; self->checkDeadline(); }
        });
    }

    void advance() {
        if (done->load()) return;
        if (cancelRequested.load()) {
            complete(GraphResult::failure({ErrorCode::Cancelled, "JobGraph cancelled."})); return;
        }
        if (expired()) { complete(GraphResult::failure({ErrorCode::Timeout, "JobGraph deadline exceeded."})); return; }
        if (next == steps.size()) { complete(GraphResult::success(std::move(value))); return; }
        auto current = ++generation;
        std::weak_ptr<GraphRun> weak = shared_from_this();
        // This gate prevents a malicious/buggy asynchronous stage from delivering
        // success and failure (or success twice) into the next stage.
        auto delivered = std::make_shared<std::atomic_bool>(false);
        auto completion = [weak, current, delivered](GraphResult result) mutable {
            if (delivered->exchange(true)) return;
            auto self = weak.lock();
            if (!self || self->done->load()) return;
            self->loop.AddTask([self, current, result = std::move(result)]() mutable {
                if (self->done->load() || self->generation != current) return;
                self->active = {};
                if (self->cancelRequested.load()) {
                    self->complete(GraphResult::failure({ErrorCode::Cancelled, "JobGraph cancelled."}));
                } else if (self->expired()) {
                    self->complete(GraphResult::failure({ErrorCode::Timeout, "JobGraph deadline exceeded."}));
                } else if (!result.ok()) self->complete(GraphResult::failure(std::move(result.error)));
                else { self->value = std::move(*result.value); self->advance(); }
            });
        };
        try {
            auto index = next++;
            active = steps[index](value, std::move(completion));
        } catch (...) {
            complete(GraphResult::failure({ErrorCode::UserException, "JobGraph step threw an exception."}));
        }
    }
};
} // namespace

OperationHandle dispatchGraph(FEventLoop& loop, std::vector<GraphStep> steps,
                              GraphDone finish, std::chrono::milliseconds timeout) {
    if (loop.HasStoped()) throw std::invalid_argument("JobGraph requires a live EventLoop.");
    if (timeout.count() < 0) throw std::invalid_argument("JobGraph timeout cannot be negative.");
    auto run = std::make_shared<GraphRun>(loop, std::move(steps), std::move(finish));
    run->keepAlive = run;
    run->hasDeadline = timeout.count() > 0;
    if (run->hasDeadline) {
        const auto now = std::chrono::steady_clock::now();
        const auto maximumDelay = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::time_point::max() - now);
        run->deadline = timeout >= maximumDelay ? std::chrono::steady_clock::time_point::max() : now + timeout;
    }
    std::weak_ptr<GraphRun> weak = run;
    auto handle = OperationHandle(nextOperationId(), [weak] {
        if (auto run = weak.lock()) {
            run->cancelRequested.store(true);
            run->loop.AddTask([run] {
                run->complete(GraphResult::failure({ErrorCode::Cancelled, "JobGraph cancelled."}));
            });
        }
    }, run->done);
    loop.AddTask([run] {
        if (run->done->load()) return;
        if (run->hasDeadline) run->checkDeadline();
        run->advance();
    });
    return handle;
}
} // namespace Fei::Async::detail
