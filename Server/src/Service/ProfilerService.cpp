#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "ProfilerService.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace Blog {
namespace {
using Json=nlohmann::json;
namespace fs=std::filesystem;
Json json(const Fei::FProfileSnapshot& snapshot){
    Json rows=Json::array();
    for(const auto& row:snapshot.rows){const auto& label=row.label;
        rows.push_back({{"id",row.id},{"name",label.name},{"category",label.category},{"file",label.file},{"line",label.line},
            {"script",label.script},{"hook",label.name},{"generation",label.generation},{"sourceVersion",label.sourceVersion},
            {"calls",row.calls},{"errors",row.errors},{"totalUs",row.totalNs/1000.0},
            {"averageUs",row.calls?row.totalNs/1000.0/row.calls:0},{"maxUs",row.maxNs/1000.0}});
    }
    return {{"enabled",snapshot.enabled},{"session",snapshot.session},{"rows",std::move(rows)},
        {"droppedSamples",snapshot.droppedSamples},{"droppedThreads",snapshot.droppedThreads},
        {"droppedSeries",snapshot.rejectedLabels},{"failedSamples",snapshot.failedSamples},{"writeError",snapshot.sinkError},
        {"timing","inclusive wall time; asynchronous spans include awaited work"}};
}
void replace(const fs::path& from,const fs::path& to){
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace profiling summary");
#else
    fs::rename(from,to);
#endif
}
}
struct ProfilerService::State {
    fs::path path;
    std::mutex stopMutex;
    bool stopped=false,started=false;
    void write(const Fei::FProfileBatch& batch){
        auto sampleFile=path/"samples.jsonl";
        if(fs::exists(sampleFile)&&fs::file_size(sampleFile)>8*1024*1024){
            fs::remove(path/"samples.3.jsonl");
            for(int i=2;i>=1;--i)if(fs::exists(path/("samples."+std::to_string(i)+".jsonl")))fs::rename(path/("samples."+std::to_string(i)+".jsonl"),path/("samples."+std::to_string(i+1)+".jsonl"));
            fs::rename(sampleFile,path/"samples.1.jsonl");
        }
        if(!batch.samples.empty()){
            std::ofstream out(sampleFile,std::ios::app|std::ios::binary);
            for(const auto& sample:batch.samples)out<<Json({{"id",sample.id},{"thread",sample.thread},{"endNs",sample.endNs},{"elapsedUs",sample.elapsedNs/1000.0},{"success",!sample.failed}}).dump()<<'\n';
            out.close();if(!out)throw std::runtime_error("Cannot write profiling samples");
        }
        auto summary=json(batch.summary);summary["directory"]=path.string();
        // A successful write resolves an earlier sink error.
        summary["writeError"]="";
        auto temporary=path/"summary.tmp";std::ofstream stats(temporary,std::ios::binary|std::ios::trunc);stats<<summary.dump(2)<<'\n';stats.close();
        if(!stats)throw std::runtime_error("Cannot write profiling summary");
        replace(temporary,path/"summary.json");
    }
};
ProfilerService::ProfilerService(std::shared_ptr<State> state):state_(std::move(state)){}
ProfilerService::~ProfilerService(){shutdown();}
std::shared_ptr<ProfilerService> ProfilerService::create(std::string directory){
#if !FEILIB_ENABLE_PROFILING
    (void)directory;throw std::logic_error("Profiling was disabled at compile time");
#endif
    if(current()||Fei::FProfiler::enabled())throw std::logic_error("Profiler already configured");
    if(directory.empty())throw std::invalid_argument("ProfilingDirectory cannot be empty");
    auto state=std::make_shared<State>();fs::create_directories(directory);
    state->path=fs::canonical(directory)/("run-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(state->path))throw std::runtime_error("Cannot create profiling run directory");
    auto service=std::shared_ptr<ProfilerService>(new ProfilerService(state));
    if(!Fei::FProfiler::start({},[state](const auto& batch){state->write(batch);}))throw std::logic_error("Profiling is unavailable or already running");
    state->started=true;
    current_.store(service);return service;
}
nlohmann::json ProfilerService::status()const{auto value=json(Fei::FProfiler::snapshot());value["directory"]=state_->path.string();return value;}
void ProfilerService::version(const std::string& script,std::uint64_t generation,const std::string& version){Fei::FProfiler::sourceVersion(script,generation,version);}
void ProfilerService::shutdown(){
    std::lock_guard lock(state_->stopMutex);if(state_->stopped)return;
    if(state_->started)Fei::FProfiler::stop();
    state_->stopped=true;
    auto current=current_.load();if(current.get()==this)current_.store({});
}
}
