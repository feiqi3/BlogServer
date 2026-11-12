#include "FEventLoop.h"
#include "FTimeQueue.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// No socket, network, logger, TLS, database or new async module is required.
class EmptyListener final : public Fei::FListener {
public:
    void listen(Fei::uint32 timeout, std::vector<Fei::FEventPtr>&) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
    }
    void addEvent(Fei::FEvent*) override {}
    void removeEvent(Fei::FEvent*) override {}
    void updateEvent(Fei::FEvent*) override {}
};

void timerInitialization() {
    // Deliberately default-initialize on nonzero storage. An uninitialized
    // scalar counter otherwise often happens to contain zero and hides the bug.
    alignas(Fei::FTimeQueue) unsigned char storage[sizeof(Fei::FTimeQueue)];
    std::memset(storage, 0xA5, sizeof storage);
    auto* queue = new (storage) Fei::FTimeQueue;
    auto now = std::chrono::system_clock::now();
    int called = 0;
    auto first = queue->AddTask([&] { ++called; }, now);
    auto second = queue->AddTask([&] { called += 100; }, now);
    queue->CancelTask(second);
    queue->CancelTask(second); // Cancellation remains idempotent.
    auto third = queue->AddTask([&] { called += 10; }, now);
    auto callbacks = queue->Tick(now);
    for (auto& callback : callbacks) callback();
    auto remaining = queue->Tick(now);
    queue->~FTimeQueue();
    require(first == 1 && second == 2 && third == 3,
            "Default construction must initialize timer IDs to 1, 2, 3");
    require(called == 11 && remaining.empty(), "Cancelled timer must not fire; timers fire once");
}

void threadLocalOwnership() {
    require(Fei::FEventLoop::getCurrentLoop() == nullptr, "Test thread must begin without a Loop");
    for (bool force : {false, true}) {
        Fei::FEventLoop loop(std::make_unique<EmptyListener>());
        bool taskOwnsThread = false;
        bool closeOwnsThread = false;
        bool foreignDidNotSteal = false;
        loop.addLoopCloseCallback([&] {
            closeOwnsThread = Fei::FEventLoop::getCurrentLoop() == &loop && loop.isInLoopThread();
        });
        loop.AddTask([&] {
            taskOwnsThread = Fei::FEventLoop::getCurrentLoop() == &loop && loop.isInLoopThread();
            // Existing behavior refuses a second active Loop on this thread.
            // Its exit must not clear the first Loop's ownership.
            Fei::FEventLoop foreign(std::make_unique<EmptyListener>());
            foreign.Loop();
            foreignDidNotSteal = Fei::FEventLoop::getCurrentLoop() == &loop;
            if (force) loop.ForceQuit();
            loop.Quit();
        });
        loop.Loop();
        require(taskOwnsThread, "Task must execute in its owning Loop");
        require(foreignDidNotSteal, "An inactive nested Loop must not clear active Loop ownership");
        require(force || closeOwnsThread, "Normal close callbacks must retain current Loop ownership");
        require(loop.HasStoped(), "Loop must report stopped after exit");
        require(Fei::FEventLoop::getCurrentLoop() == nullptr && !loop.isInLoopThread(),
                "Loop exit must clear its thread-local pointer before publishing stopped");
    }
    // Exercise a third Loop on the same thread after the earlier objects died.
    Fei::FEventLoop next(std::make_unique<EmptyListener>());
    bool ran = false;
    next.AddTask([&] { ran = true; next.Quit(); });
    next.Loop();
    require(ran, "A later Loop on the same thread must run normally");
    require(Fei::FEventLoop::getCurrentLoop() == nullptr, "No pointer to the destroyed Loop may remain");
}

void crossThreadTasks() {
    Fei::FEventLoop loop(std::make_unique<EmptyListener>());
    std::promise<void> ready;
    auto readyFuture = ready.get_future();
    std::atomic<int> calls{0};
    std::atomic<bool> wrongThread{false};
    std::atomic<bool> clearedOnExit{false};
    loop.AddTask([&] { ready.set_value(); });
    std::thread owner([&] {
        loop.Loop();
        clearedOnExit = Fei::FEventLoop::getCurrentLoop() == nullptr;
    });
    // CTest also imposes a timeout. Always join before reporting assertions.
    const bool started = readyFuture.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    if (started) {
        std::vector<std::thread> producers;
        for (int i = 0; i < 4; ++i) producers.emplace_back([&] {
            for (int n = 0; n < 100; ++n) loop.AddTask([&] {
                if (std::this_thread::get_id() != owner.get_id() || !loop.isInLoopThread()) wrongThread = true;
                ++calls;
            });
        });
        for (auto& producer : producers) producer.join();
        loop.AddTask([&] { loop.Quit(); });
    } else loop.Quit();
    owner.join();
    require(started, "Background Loop did not start");
    require(calls == 400 && !wrongThread, "Cross-thread tasks must run once on the owner thread");
    require(clearedOnExit, "Background thread must not retain Loop ownership after exit");
    require(Fei::FEventLoop::getCurrentLoop() == nullptr, "Background Loop must not affect submitting thread");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string group = argc > 1 ? argv[1] : "all";
        if (group == "all" || group == "timer") timerInitialization();
        if (group == "all" || group == "loop") threadLocalOwnership();
        if (group == "all" || group == "tasks") crossThreadTasks();
        std::cout << "PASS " << group << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
