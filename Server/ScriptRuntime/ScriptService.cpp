#include "ScriptService.h"
#include "Script/FScriptEngine.h"
#include <future>
#include <stdexcept>
namespace Blog::Script {
struct ScriptService::State {
    std::unique_ptr<Fei::FScriptEngine> engine;
    std::chrono::milliseconds interval{};
    bool started=false,closed=false;
};
ScriptService::ScriptService(Fei::FEventLoop& loop,Reporter reporter,AddressPolicy policy):state_(std::make_shared<State>()) {
    Fei::FScriptOptions options;
    options.http.allowAddress=std::move(policy);
    options.report=[reporter=std::move(reporter)](const Fei::FScriptExecution& result){if(reporter)reporter({result.phase,result.error,result.duration});};
    state_->engine=std::make_unique<Fei::FScriptEngine>(loop,std::move(options));
}
ScriptService::~ScriptService(){shutdown();}
void ScriptService::startFile(const std::string& path,std::chrono::milliseconds interval){
    if(state_->started)throw std::logic_error("Script service already started");
    loadFile(path,interval,{});
}
void ScriptService::reloadFile(const std::string& path,std::function<void()> commit){
    if(!state_->started)throw std::logic_error("Script service not started");
    loadFile(path,state_->interval,std::move(commit));
}
void ScriptService::activateFile(const std::string& path,std::chrono::milliseconds interval,std::function<void()> commit){loadFile(path,interval,std::move(commit));}
void ScriptService::loadFile(const std::string& path,std::chrono::milliseconds interval,std::function<void()> commit){
    auto& state=*state_;
    if(state.closed||state.engine->isInLoopThread())throw std::logic_error("Use the script service from its control thread");
    if(!state.started){state.engine->start();state.started=true;}
    std::promise<Fei::FScriptResult> ready;auto future=ready.get_future();
    state.engine->loadFile("blog",path,[&ready](auto result){ready.set_value(std::move(result));},interval,std::move(commit));
    auto result=future.get();
    if(!result.ok())throw std::runtime_error(result.error.message);
    state.interval=interval;
}
void ScriptService::shutdown(){
    auto& state=*state_;
    if(state.closed)return;
    if(state.engine->isInLoopThread())throw std::logic_error("Stop scripts on their control thread");
    if(state.started){std::promise<void> done;auto future=done.get_future();state.engine->shutdown([&done]{done.set_value();});future.get();}
    state.closed=true;
}
}
