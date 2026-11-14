#pragma once
#ifdef FEILIB_ENABLE_SCRIPT
#include "Script/FScriptEngine.h"
#include <atomic>
#include <memory>
#include <string>
namespace Blog::Script {
// Server-owned policy and persistence. Runtime work stays in FeiLib.
class ScriptAdminService : public std::enable_shared_from_this<ScriptAdminService> {
public:
    struct Submission { std::string id,error; };
    static std::shared_ptr<ScriptAdminService> create(Fei::FEventLoop&,Fei::FScriptEngine&,
        std::string directory,std::chrono::milliseconds interval,std::string bootstrap={});
    static std::shared_ptr<ScriptAdminService> current();
    ~ScriptAdminService();
    nlohmann::json status() const;
    nlohmann::json job(const std::string& id) const;
    Submission upload(Fei::FScriptSource source);
    Submission rollback();
    void record(const Fei::FScriptExecution&);
    void shutdown();
private:
    struct State;
    explicit ScriptAdminService(std::unique_ptr<State>);
    Submission submit(std::string kind,Fei::FScriptSource source={},std::string bootstrap={});
    std::unique_ptr<State> state_;
    inline static std::atomic<std::shared_ptr<ScriptAdminService>> current_;
};
}
#endif
