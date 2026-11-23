#include "Script/FScriptEngine.h"
#include "FEventLoop.h"
#include "FEPollListener.h"
#include "FLogger.h"
#include "FSocket.h"
#include <atomic>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
int main(int argc,char** argv){
    if(argc!=5)return 2;
    Fei::FeiInit();Fei::LoggerConfig config;config.showLvl=Fei::lvl::err;config.saveLvl=Fei::lvl::off;new Fei::Logger(config);
    int status=0;
    {
        Fei::FEventLoop loop(std::make_unique<Fei::FEPollListener>());
        Fei::FScriptOptions options;options.http.caFile=argv[3];
        options.http.allowAddress=[](auto host,auto address){return host=="127.0.0.1"&&address=="127.0.0.1";};
        Fei::FScriptEngine engine(loop,options);
        auto noCa=options;noCa.http.caFile.clear();Fei::FScriptEngine untrusted(loop,noCa);
        Fei::FScriptOptions deniedOptions;Fei::FScriptEngine denied(loop,deniedOptions);
        engine.start();untrusted.start();denied.start();
        std::thread worker([&]{loop.Loop();});
        auto get=[](auto& future){
            if(future.wait_for(8s)!=std::future_status::ready)throw std::runtime_error("Binary test deadline exceeded");
            return future.get();
        };
        auto load=[&](auto& target,Fei::FScriptSource source){
            std::promise<Fei::FScriptResult> done;auto future=done.get_future();
            target.load("binary",std::move(source),[&](auto result){done.set_value(std::move(result));});return get(future);
        };
        auto invoke=[&](auto& target,const std::string& hook,Fei::ScriptValue input){
            std::promise<Fei::FScriptResult> done;auto future=done.get_future();
            target.invoke("binary",hook,std::move(input),[&](auto result){
                if(!loop.isInLoopThread())std::terminate();done.set_value(std::move(result));
            },5s);return get(future);
        };
        auto require=[](bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);};
        auto expect=[&](auto result){require(result.ok(),result.error.message);return result.value;};
        try{
            auto source=Fei::FScriptEngine::readSource(argv[4]);
            expect(load(engine,source));expect(load(untrusted,source));expect(load(denied,source));
            const std::string plain=argv[1],tls=argv[2];
            for(const auto& base:{plain,tls}){
                for(const char* path:{"/bytes","/split","/chunked"})expect(invoke(engine,"fetch",{{"url",base+path}}));
                for(const char* hook:{"range","metadata","legacy","invalid","limits","quota"})expect(invoke(engine,hook,{{"url",base}}));
            }
            require(!invoke(untrusted,"fetch",{{"url",tls+"/untrusted-never"}}).ok(),"Untrusted certificate accepted");
            require(!invoke(denied,"fetch",{{"url",plain+"/denied-never"}}).ok(),"Private-address policy bypassed");
            auto invalid=load(engine,{{{"main.mjs","ctx.http.get('"+plain+"/validation-never',{responseType:'arrayBuffer'});"}},"main.mjs"});
            require(!invalid.ok(),"Validation started binary I/O");
            expect(invoke(engine,"memory",{{"url",plain}}));
            std::vector<std::future<Fei::FScriptResult>> requests;
            std::vector<std::shared_ptr<std::promise<Fei::FScriptResult>>> promises;
            for(int i=0;i<20;++i){
                auto done=std::make_shared<std::promise<Fei::FScriptResult>>();requests.push_back(done->get_future());promises.push_back(done);
                engine.invoke("binary","fetch",{{"url",(i%2?tls:plain)+"/bytes?id="+std::to_string(i)},{"index",i}},[done](auto value){done->set_value(std::move(value));});
            }
            for(int i=0;i<20;++i)require(expect(get(requests[i]))["index"]==i,"Concurrent binary response identity lost");
            std::promise<Fei::FScriptResult> cancelled,timed;
            auto cancelledFuture=cancelled.get_future(),timedFuture=timed.get_future();std::atomic_int cancelledCalls=0,timedCalls=0;
            auto handle=engine.invoke("binary","slow",{{"url",plain}},[&](auto result){++cancelledCalls;cancelled.set_value(std::move(result));});
            engine.invoke("binary","slow",{{"url",tls}},[&](auto result){++timedCalls;timed.set_value(std::move(result));},40ms);
            loop.RunAfter(30,[handle]{handle.cancel();});
            require(get(cancelledFuture).error.code==Fei::Async::ErrorCode::Cancelled,"Binary cancellation missing");
            require(get(timedFuture).error.code==Fei::Async::ErrorCode::Timeout,"Binary invocation timeout missing");
            std::this_thread::sleep_for(550ms);
            require(cancelledCalls==1&&timedCalls==1&&expect(invoke(engine,"stats",nullptr))["late"]==0,"Late response resumed ended invocation");
            expect(invoke(engine,"quota",{{"url",plain}}));
            std::promise<Fei::FScriptResult> oldCall;auto oldFuture=oldCall.get_future();
            engine.invoke("binary","delayed",{{"url",plain}},[&](auto result){oldCall.set_value(std::move(result));});
            expect(load(engine,{{{"main.mjs","export function healthy(){return true}"}},"main.mjs"}));
            require(expect(get(oldFuture))=="retired buffer intact","Reload invalidated a binary body");
            require(expect(invoke(engine,"healthy",nullptr))==true,"New version unavailable");
            // Leave a retained buffer in another VM and drain while a request is pending.
            expect(invoke(untrusted,"keep",{{"url",plain}}));
            std::promise<Fei::FScriptResult> stopped;auto stoppedFuture=stopped.get_future();
            untrusted.invoke("binary","slow",{{"url",plain}},[&](auto result){stopped.set_value(std::move(result));});
            std::promise<void> closed;auto closedFuture=closed.get_future();untrusted.shutdown([&]{closed.set_value();});
            require(get(stoppedFuture).error.code==Fei::Async::ErrorCode::Stopped,"Shutdown did not cancel binary work");get(closedFuture);
            std::cout<<"PASS binary HTTP: HTTP/TLS exact bytes, ArrayBuffer/DataView, range, headers, framing, limits, heap recovery, 20 invocations, cancellation, timeout, reload and shutdown\n";
        }catch(const std::exception& error){std::cerr<<error.what()<<'\n';status=1;}
        for(auto* target:{&engine,&untrusted,&denied}){
            std::promise<void> done;auto future=done.get_future();target->shutdown([&]{done.set_value();});
            if(future.wait_for(8s)!=std::future_status::ready)std::terminate();future.get();
        }
        loop.Quit();worker.join();
    }
    delete Fei::Logger::instance();Fei::FeiUnInit();return status;
}
