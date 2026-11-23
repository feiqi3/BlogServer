#include "ScriptService.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include "FSocket.h"
#include <future>
#include <thread>
#include <iostream>
using namespace std::chrono_literals;
int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) return 2;
    const std::string mode = argc == 3 ? argv[2] : "success";
    Fei::FeiInit();
    Fei::LoggerConfig cfg;
    cfg.saveFileName = "script-service-test.log";
    new Fei::Logger(cfg);
    int status = 0;
    {
        Fei::FEventLoop loop(std::make_unique<Fei::FEPollListener>());
        std::thread worker([&] { loop.Loop(); });
        std::promise<void> enough;
        auto future = enough.get_future();
        int ticks = 0;
        bool notified = false;
        std::string error;
        Blog::Script::ScriptService service(loop, [&](const auto& result) {
            if (!result.error.empty()) error = result.error;
            if (result.phase == "tick") {
                ++ticks;
                if (!notified && ((mode == "tick-failure" && !result.error.empty()) || ticks == 3)) {
                    notified = true;
                    enough.set_value();
                }
            }
        }, [](std::string_view host, std::string_view address) { return host == "127.0.0.1" && address == "127.0.0.1"; });
        try {
            service.startFile(argv[1], 20ms);
            if (mode == "start-failure") throw std::logic_error("Expected startup rejection");
            if (future.wait_for(8s) != std::future_status::ready) throw std::runtime_error("Ticks did not finish");
            service.shutdown();
            if (mode == "tick-failure") {
                if (error.empty() || ticks != 1) throw std::runtime_error("Failed tick did not pause the schedule");
            } else if (!error.empty()) throw std::runtime_error(error);
        } catch (const std::exception& e) {
            if (mode != "start-failure" || error.empty()) { std::cerr << e.what() << '\n'; status = 1; }
            service.shutdown();
        }
        loop.Quit();
        worker.join();
    }
    delete Fei::Logger::instance();
    Fei::FeiUnInit();
    return status;
}
