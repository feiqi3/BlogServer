#pragma once
#include "Async/FAsyncTypes.h"
#include "Async/FJobPool.h"
#include "FEventLoop.h"
#include "Http/FHttpClient.h"
#include <any>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Fei::Async {
namespace detail {
using GraphValue = std::any;
using GraphResult = Result<GraphValue>;
using GraphDone = std::function<void(GraphResult)>;
using GraphStep = std::function<OperationHandle(const GraphValue&, GraphDone)>;

F_API OperationHandle dispatchGraph(FEventLoop& loop, std::vector<GraphStep> steps,
                                    GraphDone finish, std::chrono::milliseconds timeout);

template<class Fn, class T>
decltype(auto) invokeGraphFunction(Fn& fn, const T& input, CancellationToken token = {}) {
    if constexpr (std::is_invocable_v<Fn&, const T&, CancellationToken>) return fn(input, token);
    else if constexpr (std::is_invocable_v<Fn&, const T&>) return fn(input);
    else if constexpr (std::is_same_v<T, std::monostate> && std::is_invocable_v<Fn&, CancellationToken>) return fn(token);
    else if constexpr (std::is_same_v<T, std::monostate> && std::is_invocable_v<Fn&>) return fn();
    else static_assert(std::is_invocable_v<Fn&, const T&>, "Step must accept the previous result (and optional CancellationToken).");
}

template<class Fn, class T>
using RawStepResult = decltype(invokeGraphFunction(std::declval<Fn&>(), std::declval<const T&>()));
template<class Fn, class T>
using StepResult = std::conditional_t<std::is_void_v<RawStepResult<Fn, T>>, std::monostate,
                                      std::decay_t<RawStepResult<Fn, T>>>;

template<class Fn, class T>
std::any invokeBoxed(Fn& fn, const std::any& input, CancellationToken token = {}) {
    using U = StepResult<Fn, T>;
    static_assert(std::is_copy_constructible_v<U>, "JobGraph values must be copyable.");
    const T& value = std::any_cast<const T&>(input);
    if constexpr (std::is_void_v<RawStepResult<Fn, T>>) {
        invokeGraphFunction(fn, value, token);
        return std::monostate{};
    } else return std::any(invokeGraphFunction(fn, value, token));
}
} // namespace detail

template<class T> class JobChain;

// A sequential Promise-like chain, not a general DAG scheduler. Construction is
// inert; dispatch snapshots the chain. Dispatching a builder twice starts two
// independent runs. All continuations/final handlers run in the bound Loop.
class JobGraph {
public:
    static JobChain<std::monostate> begin(FEventLoop& loop);
};

template<class T>
class JobChain {
public:
    // Short, nonblocking transform on the graph's Loop.
    template<class Fn>
    auto then(Fn fn) const -> JobChain<detail::StepResult<Fn, T>> {
        using U = detail::StepResult<Fn, T>;
        return append<U>([fn = std::move(fn)](const std::any& input, detail::GraphDone done) mutable {
            done(detail::GraphResult::success(detail::invokeBoxed<Fn, T>(fn, input)));
            return OperationHandle{};
        });
    }

    // Work executes in pool; only the result/next continuation returns to Loop.
    template<class Fn>
    auto thenJob(JobPool& pool, Fn fn) const -> JobChain<detail::StepResult<Fn, T>> {
        using U = detail::StepResult<Fn, T>;
        auto* loop = loop_;
        return append<U>([&pool, loop, fn = std::move(fn)](const std::any& input, detail::GraphDone done) mutable {
            // Copy input/callable per dispatch. A worker never references a
            // graph's mutable result storage or another dispatch's callable.
            return pool.submit([input, work = fn](CancellationToken token) mutable {
                return detail::invokeBoxed<Fn, T>(work, input, token);
            }, std::move(done), loop);
        });
    }

    JobChain<Http::ClientResponse> thenHttp(Http::FHttpClient& client, Http::ClientRequest request) const {
        return thenHttp(client, [request = std::move(request)](const T&) { return request; });
    }

    template<class Builder>
    JobChain<Http::ClientResponse> thenHttp(Http::FHttpClient& client, Builder builder) const {
        return append<Http::ClientResponse>([&client, builder = std::move(builder)]
                                            (const std::any& input, detail::GraphDone done) mutable {
            auto request = detail::invokeGraphFunction(builder, std::any_cast<const T&>(input));
            return client.submit(std::move(request), [done = std::move(done)](Result<Http::ClientResponse> result) mutable {
                if (result.ok()) done(detail::GraphResult::success(std::any(std::move(*result.value))));
                else done(detail::GraphResult::failure(std::move(result.error)));
            });
        });
    }

    // Adapter for another callback-style asynchronous operation. start receives
    // the previous value and a one-shot completion, returning its cancel handle.
    // Late/duplicate completions are ignored by the graph runtime.
    template<class U, class Starter>
    JobChain<U> thenAsync(Starter start) const {
        return append<U>([start = std::move(start)](const std::any& input, detail::GraphDone done) mutable {
            return start(std::any_cast<const T&>(input),
                         [done = std::move(done)](Result<U> result) mutable {
                if (result.ok()) done(detail::GraphResult::success(std::any(std::move(*result.value))));
                else done(detail::GraphResult::failure(std::move(result.error)));
            });
        });
    }

    // Typed final-value handler: append all steps before registering it.
    // onError may be registered earlier and survives later appended steps.
    JobChain onFinish(std::function<void(const T&)> callback) const {
        auto copy = *this; copy.success_ = std::move(callback); return copy;
    }
    JobChain onError(std::function<void(const Error&)> callback) const {
        auto copy = *this; copy.failure_ = std::move(callback); return copy;
    }

    OperationHandle dispatch(std::chrono::milliseconds timeout = std::chrono::milliseconds{0}) const {
        auto success = success_;
        auto failure = failure_;
        return detail::dispatchGraph(*loop_, steps_,
            [success = std::move(success), failure = std::move(failure)](detail::GraphResult result) {
                // Runtime contains handler exceptions; a throwing success
                // handler does not become a second final error notification.
                if (result.ok()) { if (success) success(std::any_cast<const T&>(*result.value)); }
                else if (failure) failure(result.error);
            }, timeout);
    }

    std::size_t size() const noexcept { return steps_.size(); }

private:
    friend class JobGraph;
    template<class U> friend class JobChain;
    explicit JobChain(FEventLoop& loop) : loop_(&loop) {}
    JobChain(FEventLoop& loop, std::vector<detail::GraphStep> steps) : loop_(&loop), steps_(std::move(steps)) {}

    template<class U>
    JobChain<U> append(detail::GraphStep step) const {
        if (success_) throw std::logic_error("Append JobGraph steps before registering the final onFinish handler.");
        if (steps_.size() >= 4096) throw std::length_error("JobGraph supports at most 4096 sequential steps.");
        auto next = steps_;
        next.push_back(std::move(step));
        auto chain = JobChain<U>(*loop_, std::move(next));
        // Error handling is independent of the final value's type and remains
        // in force when callers append type-changing steps after onError().
        chain.failure_ = failure_;
        return chain;
    }

    FEventLoop* loop_;
    std::vector<detail::GraphStep> steps_;
    std::function<void(const T&)> success_;
    std::function<void(const Error&)> failure_;
};

inline JobChain<std::monostate> JobGraph::begin(FEventLoop& loop) { return JobChain<std::monostate>(loop); }
} // namespace Fei::Async
