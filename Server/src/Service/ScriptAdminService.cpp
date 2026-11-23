#ifdef FEILIB_ENABLE_SCRIPT
#include "Service/ScriptAdminService.h"
#include "Service/ProfilerService.h"
#include "ScriptRuntime/VersionStore.h"
#include "Async/FJobPool.h"
#include "FEventLoop.h"
#include <future>
#include <mutex>
#include <deque>
#include <stdexcept>

namespace Blog::Script {
namespace {
long long now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string brief(std::string value){if(value.size()>2048)value.resize(2048);return value;}
}
struct ScriptAdminService::State {
    State(Fei::FEventLoop& owner,Fei::FScriptEngine& runtime,std::string directory,std::chrono::milliseconds delay)
        :loop(owner),engine(runtime),store(std::move(directory)),interval(delay),workers({1,1}){}
    Fei::FEventLoop& loop;
    Fei::FScriptEngine& engine;
    VersionStore store;
    std::chrono::milliseconds interval;
    Fei::Async::JobPool workers;
    mutable std::mutex mutex;
    std::mutex shutdownMutex;
    bool accepting=true,busy=false,ready=false,closed=false;
    nlohmann::json cached={{"current",""},{"previous",""},{"versions",nlohmann::json::array()}};
    std::map<std::string,nlohmann::json> jobs;
    std::deque<std::string> order;
    std::deque<nlohmann::json> events;
    nlohmann::json functions=nlohmann::json::array();
    std::function<void(const nlohmann::json&)> activated;
    struct Outcome {bool success=false;std::string version,error;nlohmann::json snapshot;};
    nlohmann::json snapshot(){
        auto active=store.active();
        nlohmann::json versions=nlohmann::json::array();
        for(const auto& id:store.versions()){
            try{
                auto source=store.load(id);std::size_t bytes=0;
                for(const auto& [path,text]:source.sources)bytes+=text.size();
                versions.push_back({{"id",id},{"entry",source.entry},{"bytes",bytes},{"files",source.sources.size()},{"valid",true}});
            }catch(const std::exception&){versions.push_back({{"id",id},{"valid",false}});}
        }
        return {{"current",active.current},{"previous",active.previous},{"versions",std::move(versions)},{"functions",functions}};
    }
    Outcome perform(const std::string& kind,Fei::FScriptSource source,const std::string& bootstrap){
        Outcome outcome;
        try{
            auto active=store.active();
            if(kind=="restore"){
                if(!active.current.empty())outcome.version=active.current;
                else if(!bootstrap.empty())outcome.version=store.save(Fei::FScriptEngine::readSource(bootstrap));
                else {outcome.success=true;outcome.snapshot=snapshot();return outcome;}
            }else if(kind=="upload")outcome.version=store.save(source);
            else if(kind=="rollback"){
                if(active.previous.empty())throw std::runtime_error("No previous version");
                outcome.version=active.previous;
            }
            if(kind=="upload"&&outcome.version==active.current){outcome.success=true;outcome.snapshot=snapshot();return outcome;}
            source=store.load(outcome.version);
            auto commit=store.prepareActivation(outcome.version);
            std::promise<Fei::FScriptResult> loaded;auto future=loaded.get_future();
            engine.load("blog",std::move(source),[this,&loaded](auto result){
                if(result.ok()&&activated){try{activated(result.value);}catch(...){}}
                loaded.set_value(std::move(result));
            },interval,std::move(commit));
            auto result=future.get();
            if(auto profiler=ProfilerService::current())profiler->version("blog",result.value.is_object()?result.value.value("generation",uint64_t(0)):0,outcome.version);
            if(!result.ok())throw std::runtime_error(result.error.message);
            functions=result.value.value("functions",nlohmann::json::array());
            outcome.success=true;
        }catch(const std::exception& e){outcome.error=brief(e.what());}
        catch(...){outcome.error="Script operation failed";}
        try{outcome.snapshot=snapshot();}catch(const std::exception& e){outcome.success=false;outcome.error=brief(e.what());}
        return outcome;
    }
};
ScriptAdminService::ScriptAdminService(std::unique_ptr<State> state):state_(std::move(state)){}
ScriptAdminService::~ScriptAdminService(){shutdown();}
std::shared_ptr<ScriptAdminService> ScriptAdminService::current(){return current_.load();}
std::shared_ptr<ScriptAdminService> ScriptAdminService::create(Fei::FEventLoop& loop,Fei::FScriptEngine& engine,
    std::string directory,std::chrono::milliseconds interval,std::string bootstrap,std::function<void(const nlohmann::json&)> activated){
    if(loop.HasStoped() || loop.isInLoopThread())throw std::logic_error("Create script administration on a control thread with a running Loop");
    auto service=std::shared_ptr<ScriptAdminService>(new ScriptAdminService(std::make_unique<State>(loop,engine,std::move(directory),interval)));
    service->state_->activated=std::move(activated);
    std::shared_ptr<ScriptAdminService> empty;
    if(!current_.compare_exchange_strong(empty,service))throw std::logic_error("Script administration already active");
    auto submitted=service->submit("restore",{},std::move(bootstrap));
    if(!submitted.error.empty()){service->shutdown();throw std::runtime_error(submitted.error);}
    return service;
}
ScriptAdminService::Submission ScriptAdminService::submit(std::string kind,Fei::FScriptSource source,std::string bootstrap){
    auto self=shared_from_this();
    auto id=std::to_string(Fei::Async::nextOperationId());
    std::unique_lock lock(state_->mutex);
    if(!state_->accepting)return {{},"Script administration is stopping"};
    if(state_->busy)return {{},"Another script operation is running"};
    state_->busy=true;
    state_->jobs[id]={{"id",id},{"kind",kind},{"state","queued"},{"submittedAt",now()}};
    state_->order.push_front(id);
    while(state_->order.size()>32){state_->jobs.erase(state_->order.back());state_->order.pop_back();}
    try {
    state_->workers.submit([self,id,kind=std::move(kind),source=std::move(source),bootstrap=std::move(bootstrap)](auto)->std::any{
        {std::lock_guard lock(self->state_->mutex);self->state_->jobs[id]["state"]="running";}
        return self->state_->perform(kind,std::move(source),bootstrap);
    },[self,id](Fei::Async::Result<std::any> result){
        State::Outcome outcome;
        if(result.ok())outcome=std::any_cast<State::Outcome>(std::move(*result.value));
        else outcome.error=result.error.message;
        std::lock_guard lock(self->state_->mutex);
        auto& job=self->state_->jobs[id];
        job["state"]=outcome.success?"succeeded":"failed";job["finishedAt"]=now();
        job["version"]=outcome.version;job["error"]=outcome.error;
        if(!outcome.snapshot.is_null())self->state_->cached=std::move(outcome.snapshot);
        self->state_->busy=false;self->state_->ready=true;
    },&state_->loop);
    } catch(const std::exception&) {
        state_->busy=false;
        state_->jobs[id]["state"]="failed";
        state_->jobs[id]["error"]="Could not dispatch script operation";
        return {{},"Could not dispatch script operation"};
    }
    return {id,{}};
}
ScriptAdminService::Submission ScriptAdminService::upload(Fei::FScriptSource source){return submit("upload",std::move(source));}
ScriptAdminService::Submission ScriptAdminService::rollback(){return submit("rollback");}
nlohmann::json ScriptAdminService::status() const {
    std::lock_guard lock(state_->mutex);
    auto status=state_->cached;status["enabled"]=true;status["ready"]=state_->ready;
    status["busy"]=state_->busy;status["intervalMs"]=state_->interval.count();
    status["jobs"]=nlohmann::json::array();for(const auto& id:state_->order)status["jobs"].push_back(state_->jobs.at(id));
    status["events"]=state_->events;return status;
}
nlohmann::json ScriptAdminService::job(const std::string& id)const{
    std::lock_guard lock(state_->mutex);auto it=state_->jobs.find(id);return it==state_->jobs.end()?nlohmann::json(nullptr):it->second;
}
void ScriptAdminService::record(const Fei::FScriptExecution& event){
    if(event.script!="blog"&&!event.script.empty())return;
    std::lock_guard lock(state_->mutex);
    state_->events.push_front({{"at",now()},{"phase",event.phase},{"error",brief(event.error)},{"durationMs",event.duration.count()}});
    while(state_->events.size()>64)state_->events.pop_back();
}
void ScriptAdminService::shutdown(){
    {std::lock_guard lock(state_->mutex);if(state_->closed)return;}
    if(state_->loop.isInLoopThread())throw std::logic_error("Stop script administration on its control thread");
    std::lock_guard stopping(state_->shutdownMutex);
    {std::lock_guard lock(state_->mutex);if(state_->closed)return;state_->accepting=false;}
    auto current=current_.load();if(current.get()==this)current_.store({});
    state_->workers.shutdown();
    std::promise<void> drained;auto done=drained.get_future();state_->loop.AddTask([&drained]{drained.set_value();});done.get();
    std::lock_guard lock(state_->mutex);state_->closed=true;
}
}
#endif
