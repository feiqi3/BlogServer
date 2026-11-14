#include "Script/FScriptEngine.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include "FSocket.h"
#include <array>
#include <future>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;
static int add(int a,int b){return a+b;}
static void fail(){throw std::runtime_error("native failure");}
static std::atomic_int touched{0};
static int touch(){return ++touched;}
static std::string greeting(const std::string& value){return "Hi "+value;}
static bool negate(bool value){return !value;}
static double half(double value){return value/2;}
static Fei::ScriptValue echo(const Fei::ScriptValue& value){return value;}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    Fei::FeiInit();Fei::LoggerConfig cfg;cfg.saveFileName="script-engine-test.log";new Fei::Logger(cfg);
    int status=0;
    {
        Fei::FEventLoop loop(std::make_unique<Fei::FEPollListener>());
        std::string error;
        std::atomic_int tickCount{0};
        Fei::FScriptOptions options;
        options.http.allowAddress=[](std::string_view host,std::string_view address){return host=="127.0.0.1"&&address=="127.0.0.1";};
        options.report=[&](const auto& event){
            if(event.script=="requests"&&event.phase=="tick"&&event.error.empty())++tickCount;
            if(event.script!="invalid"&&event.phase=="runtime"&&error.empty())error=event.error;
        };
        Fei::FScriptEngine engine(loop,options);
        FEI_SCRIPT_BIND(engine,"add",add);
        engine.bind("fail",fail);
        engine.bind("touch",touch);
        engine.bind("greeting",greeting);engine.bind("negate",negate);engine.bind("half",half);engine.bind("echo",echo);
        engine.start();
        std::promise<Fei::FScriptResult> loaded;
        auto loadedFuture=loaded.get_future();
        Fei::FScriptSource source{{{"main.mjs",R"(
            export function onStart(){
                if(ctx.add(2,3)!==5)throw Error('native function failed');
                if(ctx.greeting('x')!=='Hi x'||ctx.negate(true)!==false||ctx.half(3)!==1.5||ctx.echo({x:[true,2]}).x[1]!==2)throw Error('native values failed');
                let errors=0;
                try{ctx.add('2',3)}catch(e){errors++}
                try{ctx.add(2147483648,1)}catch(e){errors++}
                try{ctx.fail()}catch(e){errors++}
                if(errors!==3)throw Error('native validation failed');
            }
            export async function handle(input,scope){
                if(input.index===3)throw Error('one request failed');
                if(scope.id<=0)throw Error('scope missing');
                await new Promise(resolve=>scope.after(input.delay,resolve));
                const r=await scope.http.get(input.url);
                return input.index+':'+r.body+':'+ctx.add(1,2);
            }
            export function failNow(){throw Error('synchronous failure')}
            export function onTick(){}
        )"}},"main.mjs"};
        engine.load("requests",std::move(source),[&](auto result){loaded.set_value(std::move(result));},20ms);
        std::thread worker([&]{loop.Loop();});
        std::promise<void> complete;
        auto completeFuture=complete.get_future();
        std::array<int,22> calls{};int count=0;
        auto record=[&](std::string message){if(error.empty())error=std::move(message);};
        try{
            if(loadedFuture.wait_for(5s)!=std::future_status::ready)throw std::runtime_error("Engine startup timed out");
            auto result=loadedFuture.get();if(!result.ok())throw std::runtime_error(result.error.message);
            bool late=false;try{engine.bind("late",add);}catch(const std::logic_error&){late=true;}
            if(!late)throw std::runtime_error("Late native registration accepted");
            std::promise<Fei::FScriptResult> invalid;auto invalidFuture=invalid.get_future();
            engine.load("invalid",{{{"main.mjs","ctx.touch();"}},"main.mjs"},[&](auto result){invalid.set_value(std::move(result));});
            if(invalidFuture.wait_for(3s)!=std::future_status::ready||invalidFuture.get().ok()||touched.load()!=0)throw std::runtime_error("Validation performed a native side effect");
            std::promise<Fei::FScriptResult> cancelled;auto cancelledFuture=cancelled.get_future();
            auto candidate=engine.load("requests",{{{"main.mjs","export async function onStart(){await new Promise(r=>ctx.after(500,r));}"}},"main.mjs"},[&](auto result){cancelled.set_value(std::move(result));});
            loop.RunAfter(20,[candidate]{candidate.cancel();});
            if(cancelledFuture.wait_for(3s)!=std::future_status::ready||cancelledFuture.get().error.code!=Fei::Async::ErrorCode::Cancelled)throw std::runtime_error("Candidate cancellation failed");
            std::promise<Fei::FScriptResult> rejectedCommit;auto commitFuture=rejectedCommit.get_future();
            engine.load("requests",{{{"main.mjs","export function handle(){return 'wrong version'}"}},"main.mjs"},[&](auto value){rejectedCommit.set_value(std::move(value));},0ms,[]{throw 1;});
            if(commitFuture.wait_for(3s)!=std::future_status::ready||commitFuture.get().ok())throw std::runtime_error("Failed publication did not preserve its active version");
            std::vector<Fei::Async::OperationHandle> handles;
            const std::string url=std::string("http://127.0.0.1:")+argv[1];
            const int ticksBefore=tickCount.load();
            for(int i=0;i<22;++i){
                std::string hook=i==20?"failNow":i==21?"missing":"handle";
                Fei::ScriptValue input={{"index",i},{"delay",i==5?500:(i==4||i==6)?0:150},{"url",url+((i==4||i==6)?"/slow":"/ok")}};
                handles.push_back(engine.invoke("requests",hook,input,[&,i](Fei::FScriptResult value){
                    if(!loop.isInLoopThread())record("Completion left the script loop");
                    if(++calls[i]!=1)record("Completion ran twice");
                    using Code=Fei::Async::ErrorCode;
                    if(i==3||i==20){if(value.error.code!=Code::UserException)record("Invocation exception escaped its request");}
                    else if(i==4){if(value.error.code!=Code::Cancelled)record("Cancellation missing");}
                    else if(i==5||i==6){if(value.error.code!=Code::Timeout)record("Timeout missing");}
                    else if(i==21){if(value.error.code!=Code::InvalidArgument)record("Missing hook result wrong");}
                    else if(!value.ok()||value.value!=std::to_string(i)+":hello:3")record("Response associated with wrong invocation");
                    if(++count==22)complete.set_value();
                },(i==5||i==6)?80ms:3s));
            }
            loop.RunAfter(40,[operation=handles[4]]{operation.cancel();});
            if(completeFuture.wait_for(8s)!=std::future_status::ready)throw std::runtime_error("Concurrent invocations did not finish");
            completeFuture.get();
            if(tickCount.load()<ticksBefore+2)throw std::runtime_error("Periodic ticks paused during independent invocations");
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';status=1;}
        std::promise<void> closed;auto closedFuture=closed.get_future();
        engine.shutdown([&]{closed.set_value();});
        if(closedFuture.wait_for(8s)!=std::future_status::ready){std::cerr<<"Engine did not drain\n";status=1;}
        else closedFuture.get();
        loop.Quit();worker.join();
        if(!error.empty()){std::cerr<<error<<'\n';status=1;}
    }
    delete Fei::Logger::instance();Fei::FeiUnInit();return status;
}
