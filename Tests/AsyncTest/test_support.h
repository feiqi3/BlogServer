#pragma once
#include "FEPollListener.h"
#include "FEventLoop.h"
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace FeiTest {
inline void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> T await(std::future<T>& future, std::chrono::milliseconds timeout = std::chrono::seconds(10)) {
    check(future.wait_for(timeout) == std::future_status::ready, "Timed out waiting for async test result");
    return future.get();
}
class LoopRunner {
public:
    LoopRunner() : loop_(std::make_unique<Fei::FEPollListener>()) {
        std::promise<void> ready;
        auto future = ready.get_future();
        loop_.AddTask([&ready] { ready.set_value(); });
        thread_ = std::thread([this] { loop_.Loop(); });
        await(future);
    }
    ~LoopRunner() { loop_.Quit(); if (thread_.joinable()) thread_.join(); }
    LoopRunner(const LoopRunner&) = delete;
    Fei::FEventLoop& loop() { return loop_; }
    std::thread::id threadId() const { return thread_.get_id(); }
    template<class F> auto run(F&& fn) -> std::invoke_result_t<F> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(fn));
        auto future = task->get_future();
        loop_.AddTask([task] { (*task)(); });
        return await(future);
    }
    void drain() { run([] {}); }
private:
    Fei::FEventLoop loop_;
    std::thread thread_;
};
} // namespace FeiTest
