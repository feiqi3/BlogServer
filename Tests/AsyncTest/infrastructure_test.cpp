#include "test_support.h"
#include "FBuffer.h"
#include "FBufferReader.h"
#include "FTimeQueue.h"
#include "Http/FHttpRequestParser.h"
#include <atomic>
#include <cstring>
#include <new>
#include <vector>

namespace {
class EmptyListener final : public Fei::FListener {
public:
    void listen(Fei::uint32, std::vector<Fei::FEventPtr>&) override {}
    void addEvent(Fei::FEvent*) override {}
    void removeEvent(Fei::FEvent*) override {}
    void updateEvent(Fei::FEvent*) override {}
};
}

void runInfrastructureTests() {
    using namespace FeiTest;
    // Poison storage so missing scalar initialization is deterministic, not a lucky zero.
    alignas(Fei::FTimeQueue) unsigned char memory[sizeof(Fei::FTimeQueue)];
    std::memset(memory, 0xA5, sizeof memory);
    auto* timers = new (memory) Fei::FTimeQueue;
    int fired = 0;
    auto now = std::chrono::system_clock::now();
    auto first = timers->AddTask([&] { ++fired; }, now);
    auto second = timers->AddTask([&] { fired += 10; }, now);
    timers->CancelTask(second);
    auto callbacks = timers->Tick(now);
    for (auto& callback : callbacks) callback();
    timers->~FTimeQueue();
    check(first == 1 && second == 2, "Timer IDs must be initialized and monotonic");
    check(fired == 1, "Cancelled timer must not fire");

    for (int i = 0; i < 2; ++i) {
        Fei::FEventLoop loop(std::make_unique<EmptyListener>());
        bool ran = false;
        loop.AddTask([&] {
            ran = Fei::FEventLoop::getCurrentLoop() == &loop && loop.isInLoopThread();
            loop.Quit();
        });
        loop.Loop();
        check(ran, "Sequential Loops on one thread must execute their tasks");
        check(Fei::FEventLoop::getCurrentLoop() == nullptr, "Exited Loop must clear thread-local ownership");
    }

    {
        LoopRunner runner;
        std::vector<std::thread> producers;
        std::atomic<int> completed{0};
        std::atomic<bool> wrongThread{false};
        for (int i = 0; i < 4; ++i) {
            producers.emplace_back([&] {
                for (int n = 0; n < 100; ++n) runner.loop().AddTask([&] {
                    if (std::this_thread::get_id() != runner.threadId()) wrongThread = true;
                    ++completed;
                });
            });
        }
        for (auto& thread : producers) thread.join();
        runner.drain();
        check(completed == 400 && !wrongThread, "Cross-thread AddTask must run exactly once on owner Loop");
    }

    // Legacy incoming request parser remains exercised without editing its implementation.
    Fei::Http::FHttpParser parser;
    Fei::FBuffer buffer(64);
    const std::vector<std::string> parts = {"PO", "ST /submit?q=7 HTTP/1.1\r\nHost: local\r\nContent-Len",
        "gth: 5\r\n\r\nhe", "llo"};
    for (const auto& part : parts) {
        buffer.Append(part.data(), static_cast<Fei::uint32>(part.size()));
        Fei::FBufferReader reader(buffer);
        parser.parse(reader);
    }
    check(parser.getState() == Fei::Http::FHttpParser::EState::Done, "Legacy fragmented request must still parse");
    check(parser.getContext().getRequestBody() == "hello", "Legacy request body changed");
    check(parser.getContext().getRequestPath() == "/submit", "Legacy request path changed");
}
