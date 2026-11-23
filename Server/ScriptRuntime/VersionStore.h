#pragma once
#include "ModuleBundle.h"
#include <filesystem>
#include <string>
#include <functional>
#include <vector>
namespace Blog::Script {
struct ActiveVersion { std::string current, previous; };
// Serialized control-thread operations on a server-owned directory.
class VersionStore {
public:
    explicit VersionStore(std::filesystem::path root);
    std::string save(const ModuleBundle& bundle);
    ModuleBundle load(const std::string& version) const;
    std::filesystem::path entryPath(const std::string& version) const;
    ActiveVersion active() const;
    std::vector<std::string> versions() const;
    void activate(const std::string& version);
    // Validate and write on the control thread; the returned commit only renames metadata.
    std::function<void()> prepareActivation(const std::string& version);
private:
    std::filesystem::path root_;
};
}
