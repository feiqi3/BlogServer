#include "ScriptVersions.h"
#include <stdexcept>
namespace Blog::Script {
ScriptVersions::ScriptVersions(Fei::FEventLoop& loop, const std::string& root, std::chrono::milliseconds interval,
    ScriptService::Reporter reporter, ScriptService::AddressPolicy policy)
    : store_(root), loop_(loop), interval_(interval), reporter_(std::move(reporter)), policy_(std::move(policy)) {}
void ScriptVersions::restore() {
    auto record = store_.active();
    if (!record.current.empty()) activate(record.current);
}
std::string ScriptVersions::upload(const ModuleBundle& bundle) {
    auto version = store_.save(bundle);
    activate(version);
    return version;
}
void ScriptVersions::activate(const std::string& version) {
    if (service_ && store_.active().current == version) { store_.load(version); return; }
    auto commit = store_.prepareActivation(version);
    const bool first = !service_;
    if (first) service_ = std::make_unique<ScriptService>(loop_, reporter_, policy_);
    try {
        service_->activateFile(store_.entryPath(version).string(), interval_, std::move(commit));
    } catch (...) { if (first) service_.reset(); throw; }
}
void ScriptVersions::rollback() {
    const auto record = store_.active();
    if (record.previous.empty()) throw std::runtime_error("No previous script version");
    activate(record.previous);
}
void ScriptVersions::shutdown() {
    if (service_) service_->shutdown();
    service_.reset();
}
}
