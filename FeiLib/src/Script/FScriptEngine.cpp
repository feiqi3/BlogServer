#include "Script/FScriptEngine.h"
#include "FeiHttpBinding.h"
#include "ModuleBundle.h"
#include "NetworkPolicy.h"
#include "FEventLoop.h"
#include "Async/FJobPool.h"
#include "Profiling/FProfiler.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <utility>
#include <filesystem>

namespace Fei {
using namespace std::chrono_literals;
namespace {
using VM=Script::FeiHttpBinding;
FScriptResult failure(Async::ErrorCode code,std::string text) { return {nullptr,{code,std::move(text)}}; }
struct Request {
    std::uint64_t id=Async::nextOperationId();
    std::shared_ptr<std::atomic_bool> done=std::make_shared<std::atomic_bool>(false);
    std::atomic_bool cancelled{false};
    Async::OperationHandle operation;
    FScriptEngine::Completion callback;
    void finish(FScriptResult result) {
        if(done->exchange(true))return;
        try{if(callback)callback(std::move(result));}catch(...){}
    }
};
struct SourceRead { std::optional<FScriptSource> source; std::string error; };
bool validName(const std::string& name) {
    return !name.empty()&&name.size()<=64&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")==std::string::npos;
}
}
struct FScriptEngine::State : std::enable_shared_from_this<State> {
    State(FEventLoop& owner,FScriptOptions input):loop(owner),options(std::move(input)){
        if(options.maxJsonBytes<1024||options.maxJsonBytes>1024*1024)throw std::invalid_argument("Invalid script JSON budget");
        if(options.memoryLimitBytes<1024*1024||options.memoryLimitBytes>1024ULL*1024*1024)
            throw std::invalid_argument("Script memoryLimitBytes must be 1048576..1073741824 (1..1024 MiB)");
    }
    FEventLoop& loop;
    FScriptOptions options;
    std::mutex configuration;
    std::atomic_bool started{false},accepting{false},closed{false};
    std::map<std::string,NativeFunction> functions;
    std::map<std::string,NativeAsyncFunction> asynchronous;
    std::uint64_t nextGeneration=0;
    struct Config {std::string name,file;std::chrono::milliseconds interval;};
    std::vector<Config> configured;
    std::unique_ptr<Http::FHttpClient> client;
    std::unique_ptr<Async::JobPool> files;
    struct Program {
        std::shared_ptr<VM> vm;
        std::uint64_t generation=0;
        TimerID timer=0;
        std::chrono::milliseconds interval{};
        std::map<std::string,ProfileId> profiles;
    };
    struct Load {
        std::shared_ptr<Request> request;
        std::string name;
        std::chrono::milliseconds interval;
        std::function<void()> commit;
        std::shared_ptr<VM> candidate;
        ScriptValue functions=ScriptValue::array();
        std::uint64_t generation=0;
        bool finishing=false;
        std::map<std::string,ProfileId> profiles;
    };
    std::map<std::string,Program> programs;
    std::map<std::string,std::shared_ptr<Load>> loads;
    std::map<std::uint64_t,Async::OperationHandle> fileJobs;
    std::vector<std::shared_ptr<VM>> retired;
    std::vector<std::function<void()>> shutdownWaiters;
    bool stopping=false,drainingClient=false,preparingShutdown=false;
    ProfileId profile(const std::string& name,const std::string& hook,std::uint64_t generation){
        if(!options.profileInvocations||!FProfiler::enabled())return 0;
        FProfileLabel label;label.name=hook;label.category="Script";label.script=name;label.generation=generation;
        return FProfiler::registerLabel(std::move(label));
    }
    void reportElapsed(const std::string& name,std::string phase,std::string error,std::chrono::nanoseconds elapsed,std::uint64_t generation=0,ProfileId profile=0) {
        try{
            if(profile)FProfiler::record(profile,elapsed,!error.empty());
            if(options.report)options.report({name,std::move(phase),std::move(error),std::chrono::duration_cast<std::chrono::milliseconds>(elapsed),std::chrono::duration_cast<std::chrono::microseconds>(elapsed),generation});
        }catch(...){}
    }
    void report(const std::string& name,std::string phase,std::string error,std::chrono::steady_clock::time_point begin,std::uint64_t generation=0,ProfileId profile=0){
        reportElapsed(name,std::move(phase),std::move(error),std::chrono::steady_clock::now()-begin,generation,profile);
    }
    void retire(std::shared_ptr<VM> vm,bool cancelInvocations,std::string name,std::uint64_t generation,ProfileId profile) {
        retired.push_back(vm);
        vm->stopGracefully([self=shared_from_this(),vm] {
            std::erase(self->retired,vm);self->checkDrained();
        },cancelInvocations,[self=shared_from_this(),name=std::move(name),generation,profile](std::string error,std::chrono::nanoseconds elapsed){
            self->reportElapsed(name,"stop",std::move(error),elapsed,generation,profile);
        });
    }
    void schedule(const std::string& name) {
        auto& program=programs.at(name);
        auto generation=program.generation;
        program.timer=loop.RunAfter(program.interval.count(),[self=shared_from_this(),name,generation]{self->tick(name,generation);});
    }
    void tick(const std::string& name,std::uint64_t generation) {
        auto it=programs.find(name);
        if(stopping||it==programs.end()||it->second.generation!=generation||!it->second.vm)return;
        auto vm=it->second.vm;
        auto begin=std::chrono::steady_clock::now();
        auto profile=it->second.profiles.contains("tick")?it->second.profiles.at("tick"):0;
        vm->callHook("onTick",[self=shared_from_this(),vm,name,generation,begin,profile](std::string error){
            self->report(name,"tick",error,begin,generation,profile);
            auto current=self->programs.find(name);
            if(error.empty()&&!self->stopping&&current!=self->programs.end()&&current->second.generation==generation)self->schedule(name);
        });
    }
    void finishLoad(const std::shared_ptr<Load>& load,FScriptResult result,bool closeCandidate) {
        if(load->finishing)return;
        load->finishing=true;
        auto finish=[self=shared_from_this(),load,result=std::move(result)]()mutable{
            self->loads.erase(load->name);
            auto it=self->programs.find(load->name);
            if(it!=self->programs.end()&&!it->second.vm)self->programs.erase(it);
            load->request->finish(std::move(result));self->checkDrained();
        };
        if(closeCandidate&&load->candidate)load->candidate->close(std::move(finish));else finish();
    }
    void beginLoad(const std::shared_ptr<Request>& request,std::string name,FScriptSource source,
        std::chrono::milliseconds interval,std::function<void()> commit) {
        if(stopping||!client||request->cancelled.load()) {
            request->finish(failure(request->cancelled?Async::ErrorCode::Cancelled:Async::ErrorCode::Stopped,"Script load not admitted"));return;
        }
        std::size_t total=0;bool valid=source.sources.size()<=32;
        for(const auto& [file,text]:source.sources){total+=text.size();if(file.size()>256||file.find('\0')!=std::string::npos||text.size()>128*1024)valid=false;}
        if(!valid||total>512*1024||!validName(name)||source.sources.empty()||!source.sources.contains(source.entry)||interval.count()<0||interval>24h||(interval.count()>0&&interval<10ms)) {
            request->finish(failure(Async::ErrorCode::InvalidArgument,"Invalid script source or interval"));return;
        }
        if(loads.contains(name)||loads.size()>=4||retired.size()>=4||(!programs.contains(name)&&programs.size()>=16)) {
            request->finish(failure(Async::ErrorCode::QueueFull,"Script versions are still loading or draining"));return;
        }
        auto load=std::make_shared<Load>();load->request=request;load->name=name;load->interval=interval;load->commit=std::move(commit);
        load->generation=++nextGeneration;
        loads.emplace(name,load);programs.try_emplace(name);
        for(const char* hook:{"start","tick","stop","runtime"})load->profiles[hook]=profile(name,hook,load->generation);
        auto begin=std::chrono::steady_clock::now();
        try {
            load->candidate=VM::create(*client,[self=shared_from_this(),name,generation=load->generation,id=load->profiles.at("runtime")](const std::string& error){self->report(name,"runtime",error,std::chrono::steady_clock::now(),generation,id);},options.maxJsonBytes,options.memoryLimitBytes);
            for(const auto& [binding,function]:functions)load->candidate->bindFunction(binding,function);
            for(const auto& [binding,function]:asynchronous)load->candidate->bindAsyncFunction(binding,load->name,function);
            load->candidate->setValidating(true);
            if(source.entry.ends_with(".mjs"))load->candidate->loadModule(std::move(source.sources),source.entry);
            else load->candidate->evaluate(source.sources.at(source.entry));
            load->functions=load->candidate->functionNames();
            if(options.profileInvocations&&FProfiler::enabled())for(const auto& function:load->functions){
                auto hook=function.get<std::string>();load->profiles[hook]=profile(name,hook,load->generation);
            }
            load->candidate->setValidating(false);
            load->candidate->callHook("onStart",[self=shared_from_this(),load,begin](std::string error){
                if(load->finishing)return;
                if(self->stopping||load->request->cancelled.load()) {
                    self->finishLoad(load,failure(Async::ErrorCode::Cancelled,"Script load cancelled"),true);return;
                }
                if(error.empty()&&load->commit){try{load->commit();}catch(const std::exception& e){error=e.what();}catch(...){error="Activation callback failed";}}
                self->report(load->name,"start",error,begin,load->generation,load->profiles.at("start"));
                if(!error.empty()){self->finishLoad(load,failure(Async::ErrorCode::UserException,error),true);return;}
                auto& program=self->programs.at(load->name);
                if(program.timer)self->loop.CancelTimer(program.timer);
                auto previousGeneration=program.generation;
                auto previousProfile=program.profiles.contains("stop")?program.profiles.at("stop"):0;
                auto previous=std::exchange(program.vm,load->candidate);
                program.generation=load->generation;program.interval=load->interval;
                program.profiles=std::move(load->profiles);
                if(previous)self->retire(std::move(previous),false,load->name,previousGeneration,previousProfile);
                if(program.interval.count()>0) {
                    auto generation=program.generation;
                    program.timer=self->loop.RunAfter(0,[self,name=load->name,generation]{self->tick(name,generation);});
                }
                self->finishLoad(load,{{{"functions",load->functions},{"generation",load->generation}},{}},false);
            });
        }catch(const std::exception& e){finishLoad(load,failure(Async::ErrorCode::UserException,e.what()),true);}
        catch(...){finishLoad(load,failure(Async::ErrorCode::UserException,"Script loading failed"),true);}
    }
    void fileLoad(const std::shared_ptr<Request>& request,std::string name,std::string path,
        std::chrono::milliseconds interval,std::function<void()> commit) {
        if(stopping||!files||request->cancelled.load()){request->finish(failure(request->cancelled?Async::ErrorCode::Cancelled:Async::ErrorCode::Stopped,"Engine unavailable"));return;}
        auto self=shared_from_this();
        fileJobs[request->id]=files->submit([path=std::move(path)](Async::CancellationToken token)->std::any {
            if(token.cancelled())throw std::runtime_error("Load cancelled");
            SourceRead read;
            try{read.source=FScriptEngine::readSource(path);}catch(const std::exception& e){read.error=e.what();}
            return read;
        },[self,request,name=std::move(name),interval,commit=std::move(commit)](Async::Result<std::any> result)mutable{
            self->fileJobs.erase(request->id);
            if(!result.ok())request->finish(failure(result.error.code,result.error.message));
            else if(self->stopping||request->cancelled.load())request->finish(failure(Async::ErrorCode::Cancelled,"Load cancelled"));
            else {
                auto read=std::any_cast<SourceRead>(std::move(*result.value));
                if(!read.source)request->finish(failure(Async::ErrorCode::Io,read.error));
                else self->beginLoad(request,std::move(name),std::move(*read.source),interval,std::move(commit));
            }
            self->checkDrained();
        },&loop);
    }
    void cancelLoad(const std::shared_ptr<Request>& request) {
        if(auto it=fileJobs.find(request->id);it!=fileJobs.end())it->second.cancel();
        for(const auto& [name,load]:loads)if(load->request==request){finishLoad(load,failure(Async::ErrorCode::Cancelled,"Load cancelled"),true);break;}
    }
    void checkDrained() {
        if(!stopping||preparingShutdown||!programs.empty()||!loads.empty()||!fileJobs.empty()||!retired.empty()||drainingClient)return;
        drainingClient=true;
        auto finish=[self=shared_from_this()]{
            self->client.reset();
            // All file completions have arrived: workers have no remaining I/O.
            if(self->files)self->files->shutdown();self->files.reset();
            self->closed.store(true);
            auto callbacks=std::move(self->shutdownWaiters);
            for(auto& callback:callbacks){try{if(callback)callback();}catch(...){}}
        };
        if(client)client->shutdown(std::move(finish));else finish();
    }
};
FScriptEngine::FScriptEngine(FEventLoop& loop,FScriptOptions options):state_(std::make_shared<State>(loop,std::move(options))){}
FScriptEngine::~FScriptEngine(){if(state_->started&&!state_->closed)shutdown();}
bool FScriptEngine::isInLoopThread() const{return state_->loop.isInLoopThread();}
void FScriptEngine::bindFunction(std::string name,NativeFunction function){
    std::lock_guard lock(state_->configuration);
    if(state_->started||state_->closed||!validName(name)||name=="http"||name=="after"||!function||state_->functions.contains(name)||state_->asynchronous.contains(name))throw std::logic_error("Invalid, duplicate or late native binding");
    state_->functions.emplace(std::move(name),std::move(function));
}
void FScriptEngine::bindAsyncFunction(std::string name,NativeAsyncFunction function){
    std::lock_guard lock(state_->configuration);
    if(state_->started||state_->closed||name.size()>128||!function||state_->asynchronous.contains(name)||state_->functions.contains(name))
        throw std::logic_error("Invalid, duplicate or late asynchronous binding");
    std::size_t begin=0;
    do{
        auto end=name.find('.',begin);auto part=name.substr(begin,end==std::string::npos?end:end-begin);
        if(!validName(part)||part=="__proto__"||part=="constructor"||part=="prototype"||
            (begin==0&&(part=="http"||part=="after"||part=="id")))throw std::invalid_argument("Invalid asynchronous binding name");
        if(end==std::string::npos)break;begin=end+1;
    }while(true);
    state_->asynchronous.emplace(std::move(name),std::move(function));
}
void FScriptEngine::addScript(std::string name,std::string file,std::chrono::milliseconds interval){
    std::lock_guard lock(state_->configuration);
    if(state_->started||state_->closed||!validName(name)||interval.count()<0||interval>24h||(interval.count()>0&&interval<10ms))throw std::logic_error("Configure valid scripts before start");
    for(const auto& entry:state_->configured)if(entry.name==name)throw std::invalid_argument("Duplicate script");
    state_->configured.push_back({std::move(name),std::move(file),interval});
}
void FScriptEngine::start(){
    auto self=state_;
    std::lock_guard lock(self->configuration);
    if(self->closed)throw std::logic_error("Engine already closed");
    if(self->started.exchange(true))return;
    self->accepting.store(true);
    self->loop.AddTask([self]{
        try{
            if(!self->options.http.allowAddress)self->options.http.allowAddress=Script::allowPublicAddress;
            self->client=std::make_unique<Http::FHttpClient>(self->loop,self->options.http);
            self->files=std::make_unique<Async::JobPool>(Async::JobPoolOptions{1,32});
            for(const auto& config:self->configured){
                auto request=std::make_shared<Request>();
                request->callback=[self,name=config.name](FScriptResult result){if(!result.ok())self->report(name,"load",result.error.message,std::chrono::steady_clock::now());};
                self->fileLoad(request,config.name,config.file,config.interval,{});
            }
        }catch(const std::exception& e){self->accepting.store(false);self->report("","startup",e.what(),std::chrono::steady_clock::now());}
    });
}
Async::OperationHandle FScriptEngine::load(std::string name,FScriptSource source,Completion callback,std::chrono::milliseconds interval,std::function<void()> commit){
    auto self=state_;auto request=std::make_shared<Request>();request->callback=std::move(callback);
    self->loop.AddTask([self,request,name=std::move(name),source=std::move(source),interval,commit=std::move(commit)]()mutable{
        if(!self->accepting){request->finish(failure(Async::ErrorCode::Stopped,"Engine unavailable"));return;}
        self->beginLoad(request,std::move(name),std::move(source),interval,std::move(commit));
    });
    return {request->id,[self,request]{request->cancelled=true;self->loop.AddTask([self,request]{self->cancelLoad(request);});},request->done};
}
Async::OperationHandle FScriptEngine::loadFile(std::string name,std::string file,Completion callback,std::chrono::milliseconds interval,std::function<void()> commit){
    auto self=state_;auto request=std::make_shared<Request>();request->callback=std::move(callback);
    self->loop.AddTask([self,request,name=std::move(name),file=std::move(file),interval,commit=std::move(commit)]()mutable{
        if(!self->accepting){request->finish(failure(Async::ErrorCode::Stopped,"Engine unavailable"));return;}
        self->fileLoad(request,std::move(name),std::move(file),interval,std::move(commit));
    });
    return {request->id,[self,request]{request->cancelled=true;self->loop.AddTask([self,request]{self->cancelLoad(request);});},request->done};
}
Async::OperationHandle FScriptEngine::invoke(std::string script,std::string hook,ScriptValue input,Completion callback,std::chrono::milliseconds timeout){
    auto self=state_;auto request=std::make_shared<Request>();request->callback=std::move(callback);
    std::string json,error;
    try{json=input.dump();if(json.size()>self->options.maxJsonBytes)error="Invocation input exceeds JSON budget";}catch(const std::exception& e){error=e.what();}
    auto submitted=std::chrono::steady_clock::now();
    self->loop.AddTask([self,request,script=std::move(script),hook=std::move(hook),json=std::move(json),error=std::move(error),timeout,submitted]{
        if(!error.empty()){request->finish(failure(Async::ErrorCode::InvalidArgument,error));return;}
        if(!self->accepting||request->cancelled){request->finish(failure(request->cancelled?Async::ErrorCode::Cancelled:Async::ErrorCode::Stopped,"Invocation not admitted"));return;}
        if(timeout.count()<=0||timeout>1h){request->finish(failure(Async::ErrorCode::InvalidArgument,"Invalid invocation timeout"));return;}
        auto remaining=timeout-std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-submitted);
        if(remaining.count()<=0){request->finish(failure(Async::ErrorCode::Timeout,"Invocation expired before admission"));return;}
        auto it=self->programs.find(script);
        if(it==self->programs.end()||!it->second.vm){request->finish(failure(Async::ErrorCode::InvalidArgument,"Script is not loaded"));return;}
        auto vm=it->second.vm;
        auto generation=it->second.generation;
        auto profileIt=it->second.profiles.find(hook);
        auto profile=profileIt==it->second.profiles.end()?0:profileIt->second;
        auto profileBegin=(profile||self->options.reportInvocations)?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point();
        try{
            request->operation=vm->invoke(hook,json,[self,request,vm,script=self->options.reportInvocations?script:std::string(),hook=self->options.reportInvocations?hook:std::string(),generation,profileBegin,profile](Script::InvocationResult result){
                if(self->options.reportInvocations)self->report(script,hook,result.error.message,profileBegin,generation,profile);
                else if(profile)FProfiler::record(profile,std::chrono::steady_clock::now()-profileBegin,!result.ok());
                FScriptResult output;output.error=std::move(result.error);
                if(result.ok()){try{output.value=ScriptValue::parse(result.json);}catch(const std::exception& e){output.error={Async::ErrorCode::Protocol,e.what()};}}
                request->finish(std::move(output));
            },remaining,request->id);
        }catch(const std::exception& e){request->finish(failure(Async::ErrorCode::UserException,e.what()));}
    });
    return {request->id,[self,request]{request->cancelled=true;self->loop.AddTask([request]{request->operation.cancel();});},request->done};
}
void FScriptEngine::shutdown(std::function<void()> callback){
    auto self=state_;
    {
        std::unique_lock lock(self->configuration);
        self->accepting.store(false);
        if(!self->started){self->closed=true;lock.unlock();if(callback)callback();return;}
    }
    self->loop.AddTask([self,callback=std::move(callback)]()mutable{
        if(self->closed){if(callback)callback();return;}
        if(callback)self->shutdownWaiters.push_back(std::move(callback));
        if(self->stopping)return;
        self->stopping=true;
        self->preparingShutdown=true;
        for(auto& vm:self->retired)vm->cancelInvocations();
        for(auto& [id,operation]:self->fileJobs)operation.cancel();
        std::vector<std::shared_ptr<State::Load>> candidates;
        for(auto& [name,load]:self->loads)candidates.push_back(load);
        for(auto& load:candidates)self->finishLoad(load,failure(Async::ErrorCode::Stopped,"Engine stopping"),true);
        auto programs=std::move(self->programs);self->programs.clear();
        for(auto& [name,program]:programs){
            if(program.timer)self->loop.CancelTimer(program.timer);
            if(program.vm)self->retire(std::move(program.vm),true,name,program.generation,program.profiles.contains("stop")?program.profiles.at("stop"):0);
        }
        self->preparingShutdown=false;
        self->checkDrained();
    });
}
FScriptSource FScriptEngine::readSource(const std::string& file){
    if(!file.ends_with(".js")&&!file.ends_with(".mjs"))throw std::invalid_argument("Script files must use .js or .mjs");
    if(file.ends_with(".mjs"))return Script::readModuleBundle(file);
    auto status=std::filesystem::symlink_status(file);
    if(!std::filesystem::is_regular_file(status)||std::filesystem::is_symlink(status))throw std::runtime_error("Script must be a regular file");
    std::ifstream stream(file,std::ios::binary|std::ios::ate);
    if(!stream||stream.tellg()<0||stream.tellg()>128*1024)throw std::runtime_error("Script missing or larger than 128 KiB");
    stream.seekg(0);std::string source{std::istreambuf_iterator<char>(stream),{}};
    auto entry=std::filesystem::path(file).filename().generic_string();
    return {{{entry,std::move(source)}},entry};
}
}
