#include "Async/FJobPool.h"
#include "FEventLoop.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Fei::Async {
struct JobPool::Impl : std::enable_shared_from_this<JobPool::Impl> {
    struct Task {
        std::uint64_t id = nextOperationId();
        Work work;
        Completion completion;
        FEventLoop* loop = nullptr;
        std::shared_ptr<std::atomic_bool> cancelled = std::make_shared<std::atomic_bool>(false);
        std::shared_ptr<std::atomic_bool> done = std::make_shared<std::atomic_bool>(false);
        bool started = false; // protected by mutex
        bool counted = false;
    };

    explicit Impl(JobPoolOptions value) : options(value) {}
    ~Impl() {
        // Also handles an owner released by its own last worker: workers retain
        // Impl until they stop, so thread objects never outlive live state.
        for (auto& worker : workers) if (worker.joinable()) {
            if (worker.get_id() == std::this_thread::get_id()) worker.detach();
            else worker.join();
        }
    }

    void start() {
        auto self = shared_from_this();
        try {
            for (std::size_t i = 0; i < options.threads; ++i)
                workers.emplace_back([self] { self->run(); });
        } catch (...) {
            requestStop();
            for (auto& worker : workers) if (worker.joinable()) worker.join();
            throw;
        }
    }

    void publish(const std::shared_ptr<Task>& task, Result<std::any> result) {
        if (task->done->exchange(true)) return;
        if (task->counted) --pendingTasks;
        ++notifications;
        auto self = shared_from_this();
        auto delivered = std::make_shared<Result<std::any>>(std::move(result));
        task->loop->AddTask([self, task, delivered] {
            try { if (task->completion) task->completion(std::move(*delivered)); }
            catch (...) { /* A completion handler cannot terminate the Loop. */ }
            --self->notifications;
        });
    }

    void cancel(const std::shared_ptr<Task>& task) {
        task->cancelled->store(true);
        bool removed = false;
        {
            std::lock_guard lock(mutex);
            if (!task->started && !task->done->load()) {
                for (auto it = queue.begin(); it != queue.end(); ++it) {
                    if (*it == task) { queue.erase(it); removed = true; tasks.erase(task->id); break; }
                }
            }
        }
        // Running tasks complete only when Run returns, so OnFinish never races
        // their access to mutable Job state.
        if (removed) publish(task, Result<std::any>::failure({ErrorCode::Cancelled, "Job cancelled."}));
    }

    void requestStop() {
        std::deque<std::shared_ptr<Task>> abandoned;
        {
            std::lock_guard lock(mutex);
            stopped = true;
            for (auto& [id, task] : tasks) task->cancelled->store(true);
            abandoned.swap(queue);
            for (auto& task : abandoned) tasks.erase(task->id);
        }
        for (auto& task : abandoned)
            publish(task, Result<std::any>::failure({ErrorCode::Stopped, "Job pool stopped."}));
        cv.notify_all();
    }

    void run() {
        current = this;
        for (;;) {
            std::shared_ptr<Task> task;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, [&] { return stopped || !queue.empty(); });
                if (queue.empty() && stopped) break;
                task = queue.front(); queue.pop_front(); task->started = true;
            }
            auto result = Result<std::any>::failure({ErrorCode::Cancelled, "Job cancelled."});
            if (!task->cancelled->load()) {
                try {
                    auto value = task->work(CancellationToken(task->cancelled));
                    if (!task->cancelled->load()) result = Result<std::any>::success(std::move(value));
                } catch (...) {
                    result = Result<std::any>::failure({ErrorCode::UserException, "Job Run threw an exception."});
                }
            }
            publish(task, std::move(result));
            {
                std::lock_guard lock(mutex);
                tasks.erase(task->id);
            }
        }
        current = nullptr;
    }

    JobPoolOptions options;
    mutable std::mutex mutex;
    std::mutex joinMutex;
    std::condition_variable cv;
    std::deque<std::shared_ptr<Task>> queue;
    std::unordered_map<std::uint64_t, std::shared_ptr<Task>> tasks;
    std::vector<std::thread> workers;
    bool stopped = false;
    std::atomic_size_t pendingTasks{0};
    std::atomic_size_t notifications{0};
    static thread_local Impl* current;
};

thread_local JobPool::Impl* JobPool::Impl::current = nullptr;

JobPool::JobPool(JobPoolOptions options) {
    if (!options.threads || !options.maxQueued)
        throw std::invalid_argument("JobPool requires positive thread and queue limits.");
    impl_ = std::make_shared<Impl>(options);
    impl_->start();
}

JobPool::~JobPool() {
    if (Impl::current == impl_.get()) impl_->requestStop();
    else shutdown();
}

OperationHandle JobPool::submit(Work work, Completion completion, FEventLoop* target) {
    if (!target) target = FEventLoop::getCurrentLoop();
    if (!target || target->HasStoped())
        throw std::invalid_argument("JobPool completion requires a live EventLoop.");
    auto task = std::make_shared<Impl::Task>();
    task->work = std::move(work); task->completion = std::move(completion); task->loop = target;
    std::optional<Error> rejection;
    {
        std::lock_guard lock(impl_->mutex);
        if (!task->work) rejection = Error{ErrorCode::InvalidArgument, "Job work is empty."};
        else if (impl_->stopped) rejection = Error{ErrorCode::Stopped, "Job pool stopped."};
        else if (impl_->queue.size() >= impl_->options.maxQueued)
            rejection = Error{ErrorCode::QueueFull, "Job queue is full."};
        else {
            task->counted = true;
            ++impl_->pendingTasks;
            impl_->tasks.emplace(task->id, task);
            impl_->queue.push_back(task);
        }
    }
    std::weak_ptr<Impl> weakPool = impl_;
    std::weak_ptr<Impl::Task> weakTask = task;
    auto handle = OperationHandle(task->id, [weakPool, weakTask] {
        if (auto pool = weakPool.lock()) if (auto task = weakTask.lock()) pool->cancel(task);
    }, task->done);
    if (rejection) impl_->publish(task, Result<std::any>::failure(*rejection));
    else impl_->cv.notify_one();
    return handle;
}

OperationHandle JobPool::submit(std::shared_ptr<Job> job, FEventLoop* target) {
    if (!job) throw std::invalid_argument("Cannot submit a null Job.");
    return submit([job](CancellationToken token) { return job->Run(token); },
                  [job](Result<std::any> result) { job->OnFinish(result); }, target);
}

void JobPool::shutdown() {
    if (Impl::current == impl_.get())
        throw std::logic_error("JobPool::shutdown cannot join its own worker thread.");
    std::lock_guard lock(impl_->joinMutex);
    impl_->requestStop();
    for (auto& worker : impl_->workers) if (worker.joinable()) worker.join();
}

std::size_t JobPool::pending() const noexcept { return impl_->pendingTasks.load(); }
std::size_t JobPool::pendingCompletions() const noexcept { return impl_->notifications.load(); }
} // namespace Fei::Async
