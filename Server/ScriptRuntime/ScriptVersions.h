#pragma once
#include "ScriptService.h"
#include "VersionStore.h"
namespace Blog::Script {
// Serialize these control-thread operations. Runtime publication happens on the FeiLib loop.
class ScriptVersions {
public:
    ScriptVersions(Fei::FEventLoop&, const std::string& root, std::chrono::milliseconds interval,
                   ScriptService::Reporter, ScriptService::AddressPolicy policy = {});
    void restore();
    std::string upload(const ModuleBundle&);
    void activate(const std::string& version);
    void rollback();
    ActiveVersion active() const { return store_.active(); }
    void shutdown();
private:
    VersionStore store_;
    Fei::FEventLoop& loop_;
    std::chrono::milliseconds interval_;
    ScriptService::Reporter reporter_;
    ScriptService::AddressPolicy policy_;
    std::unique_ptr<ScriptService> service_;
};
}
