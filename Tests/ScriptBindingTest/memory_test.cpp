#include "Script/FScriptEngine.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include "FSocket.h"
#include <future>
#include <iostream>
#include <limits>
#include <thread>

using namespace std::chrono_literals;
namespace {
constexpr std::size_t MiB=1024*1024;
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class T> T wait(std::future<T>& future){
    // Timeout is a test failure; never leave callbacks referring to dead promises.
    if(future.wait_for(5s)!=std::future_status::ready)std::terminate();
    return future.get();
}
Fei::FScriptSource source(unsigned mib,unsigned marker){
    return {{{"main.mjs","let buffer; export function onStart(){buffer=new Uint8Array("+
        std::to_string(mib)+"*1024*1024);buffer[0]="+std::to_string(marker)+";}"
        "export function inspect(){return buffer[0];}"
        "export function probe(bytes){try{const b=new Uint8Array(bytes);b[bytes-1]=42;return b[bytes-1]===42;}catch(error){return false;}}"}},"main.mjs"};
}
}
int main(){
    Fei::FeiInit();Fei::LoggerConfig config;config.showLvl=Fei::lvl::err;config.saveLvl=Fei::lvl::off;new Fei::Logger(config);
    int status=0;
    {
        Fei::FEventLoop loop(std::make_unique<Fei::FEPollListener>());
        for(auto bytes:{std::size_t(0),MiB-1,1024*MiB+1,std::numeric_limits<std::size_t>::max()}){
            bool rejected=false;Fei::FScriptOptions options;options.memoryLimitBytes=bytes;
            try{Fei::FScriptEngine invalid(loop,options);}catch(const std::invalid_argument&){rejected=true;}
            if(!rejected){std::cerr<<"Invalid heap cap accepted\n";status=1;}
        }
        {Fei::FScriptOptions options;options.memoryLimitBytes=1024*MiB;Fei::FScriptEngine upperBoundary(loop,options);}
        Fei::FScriptEngine defaults(loop);
        Fei::FScriptOptions larger;larger.memoryLimitBytes=16*MiB;Fei::FScriptEngine configured(loop,larger);
        Fei::FScriptOptions smallest;smallest.memoryLimitBytes=MiB;Fei::FScriptEngine minimum(loop,smallest);
        for(auto* engine:{&defaults,&configured,&minimum})engine->start();
        std::thread worker([&]{loop.Loop();});
        auto load=[&](auto& engine,const std::string& name,Fei::FScriptSource input){
            std::promise<Fei::FScriptResult> done;auto future=done.get_future();
            engine.load(name,std::move(input),[&](auto result){require(loop.isInLoopThread(),"Load completion left loop");done.set_value(std::move(result));});
            return wait(future);
        };
        auto invoke=[&](auto& engine,const std::string& name,const std::string& hook,Fei::ScriptValue input=nullptr){
            std::promise<Fei::FScriptResult> done;auto future=done.get_future();
            engine.invoke(name,hook,std::move(input),[&](auto result){done.set_value(std::move(result));});return wait(future);
        };
        auto value=[](Fei::FScriptResult result){require(result.ok(),result.error.message);return result.value;};
        try{
            value(load(minimum,"small",source(0,0)));
            value(load(defaults,"first",source(6,11)));
            value(load(defaults,"second",source(6,22)));
            require(value(invoke(defaults,"first","inspect"))==11&&value(invoke(defaults,"second","inspect"))==22,
                "Independent script heaps were combined");
            require(!load(defaults,"first",source(10,99)).ok(),"Default 8 MiB cap did not limit candidate");
            require(value(invoke(defaults,"first","inspect"))==11,"Failed candidate replaced active script");
            value(load(configured,"large",source(10,33)));
            require(value(invoke(configured,"large","inspect"))==33,"Configured 16 MiB cap was not applied");
            require(value(invoke(configured,"large","probe",24*MiB))==false,"Configured heap cap was unlimited");
            require(value(invoke(configured,"large","probe",MiB))==true,"Caught allocation failure disabled runtime");
            // Candidate and active version each retain 10 MiB simultaneously.
            value(load(configured,"large",source(10,44)));
            require(value(invoke(configured,"large","inspect"))==44,"Reload lost configured heap cap");
            require(!load(configured,"large",source(20,99)).ok(),"Reload candidate bypassed configured cap");
            require(value(invoke(configured,"large","inspect"))==44,"Over-budget reload damaged active version");
            std::cout<<"PASS configurable heaps: default/custom limits, validation, per-script/version ownership, reload failure isolation and allocation recovery\n";
        }catch(const std::exception& error){std::cerr<<error.what()<<'\n';status=1;}
        for(auto* engine:{&defaults,&configured,&minimum}){
            std::promise<void> done;auto future=done.get_future();engine->shutdown([&]{done.set_value();});wait(future);
        }
        loop.Quit();worker.join();
    }
    delete Fei::Logger::instance();Fei::FeiUnInit();return status;
}
