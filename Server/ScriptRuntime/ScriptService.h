#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
namespace Fei { class FEventLoop; }
namespace Blog::Script {
struct ExecutionResult {
    std::string phase;
    std::string error;
    std::chrono::milliseconds duration{};
};
// Control-thread owner. The supplied FeiLib loop must remain running until shutdown returns.
class ScriptService {
public:
    using Reporter = std::function<void(const ExecutionResult&)>;
    using AddressPolicy = std::function<bool(std::string_view, std::string_view)>;
    ScriptService(Fei::FEventLoop& loop, Reporter reporter, AddressPolicy policy = {});
    ~ScriptService();
    ScriptService(const ScriptService&) = delete;
    ScriptService& operator=(const ScriptService&) = delete;
    void startFile(const std::string& path, std::chrono::milliseconds interval);
    // Validate and start a candidate; keep the active version on failure.
    // commit executes immediately before the Loop publishes the new version.
    void reloadFile(const std::string& path, std::function<void()> commit = {});
    void activateFile(const std::string& path, std::chrono::milliseconds interval, std::function<void()> commit);
    void shutdown();
private:
    struct State;
    void loadFile(const std::string&, std::chrono::milliseconds, std::function<void()>);
    std::shared_ptr<State> state_;
};
}
