#pragma once
#include "Async/FAsyncTypes.h"
#include "FDef.h"
#include <any>
#include <cstddef>
#include <functional>
#include <memory>

namespace Fei { class FEventLoop; }
namespace Fei::Async {

class Job {
public:
    virtual ~Job() = default;
    virtual std::any Run(CancellationToken cancellation) = 0;
    virtual void OnFinish(const Result<std::any>&) {}
};

struct JobPoolOptions {
    std::size_t threads = 2;
    std::size_t maxQueued = 256;
};

// A bounded worker pool. Every accepted/rejected submission with a valid target
// receives exactly one completion on that target Loop. Keep the Loop alive and
// running until shutdown() has returned AND its completion queue has drained.
class F_API JobPool {
public:
    using Work = std::function<std::any(CancellationToken)>;
    using Completion = std::function<void(Result<std::any>)>;
    explicit JobPool(JobPoolOptions options = {});
    ~JobPool();
    JobPool(const JobPool&) = delete;
    JobPool& operator=(const JobPool&) = delete;

    OperationHandle submit(Work work, Completion completion,
                           FEventLoop* completionLoop = nullptr);
    OperationHandle submit(std::shared_ptr<Job> job,
                           FEventLoop* completionLoop = nullptr);

    // Stops accepting work, cancels queued work and cooperatively cancels running
    // work, then joins workers. Must not be called from this pool's worker.
    // OnFinish notifications remain asynchronous and must be drained by Loops.
    void shutdown();
    std::size_t pending() const noexcept;
    std::size_t pendingCompletions() const noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace Fei::Async
