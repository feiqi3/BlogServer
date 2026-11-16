#pragma once
#include "Profiling/FProfiler.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <memory>
#include <string>

namespace Blog {
// Server configuration, JSON presentation and file persistence for FeiLib profiling.
class ProfilerService {
public:
    static std::shared_ptr<ProfilerService> create(std::string directory);
    static std::shared_ptr<ProfilerService> current(){return current_.load();}
    ~ProfilerService();
    nlohmann::json status()const;
    void version(const std::string& script,std::uint64_t generation,const std::string& version);
    void shutdown();
private:
    struct State;
    explicit ProfilerService(std::shared_ptr<State>);
    std::shared_ptr<State> state_;
    inline static std::atomic<std::shared_ptr<ProfilerService>> current_;
};
}
