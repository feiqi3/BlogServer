#include "Script/FScriptEngine.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include "FSocket.h"
#include "Async/FJobPool.h"
#include "Profiling/FProfiler.h"
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
    const bool profilerStarted=Fei::FProfiler::start();
    int status=0;
    {
        Fei::FEventLoop loop(std::make_unique<Fei::FEPollListener>());
        std::string error;
        std::atomic_int tickCount{0};
        std::atomic_int nativeCalls{0};
        std::atomic_int profiledVersions{0};
        std::atomic_int stopFailures{0};
        std::atomic_int stopRuntimeFailures{0};
        Fei::Async::JobPool databaseWorkers({2,64});
        Fei::FScriptOptions options;
        options.maxJsonBytes=256*1024;
        options.profileInvocations=true;
        options.reportInvocations=true;
        options.http.allowAddress=[](std::string_view host,std::string_view address){return host=="127.0.0.1"&&address=="127.0.0.1";};
        options.report=[&](const auto& event){
            if(event.script=="requests"&&event.phase=="tick"&&event.error.empty())++tickCount;
            if(event.script!="invalid"&&event.phase=="runtime"){
                if(event.error.find("stop failure")!=std::string::npos)++stopRuntimeFailures;
                else if(error.empty())error=event.error;
            }
            if(event.phase=="versioned"&&event.generation==1&&event.elapsed>=200ms&&event.error.empty())++profiledVersions;
            if(event.phase=="stop"&&event.elapsed>=20ms&&event.error.find("stop failure")!=std::string::npos)++stopFailures;
        };
        Fei::FScriptEngine engine(loop,options);
        FEI_SCRIPT_BIND(engine,"add",add);
        engine.bind("fail",fail);
        engine.bind("touch",touch);
        engine.bind("greeting",greeting);engine.bind("negate",negate);engine.bind("half",half);engine.bind("echo",echo);
        engine.bindAsyncFunction("db.echo",[&](Fei::FScriptNativeCall call,Fei::FScriptEngine::Completion callback){
            ++nativeCalls;
            return databaseWorkers.submit([call=std::move(call)](auto token)->std::any{
                if(call.script!="requests"||call.hook!="handle"||!call.invocationId)throw std::runtime_error("Missing native call context");
                std::this_thread::sleep_for(5ms);
                if(token.cancelled())return Fei::ScriptValue();
                return call.arguments.at(0);
            },[callback=std::move(callback)](auto result){
                if(result.ok())callback({std::any_cast<Fei::ScriptValue>(std::move(*result.value)),{}});
                else callback({nullptr,result.error});
            },&loop);
        });
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
                if(await scope.db.echo(input.index)!==input.index)throw Error('native async result mismatch');
                const r=await scope.http.get(input.url);
                return input.index+':'+r.body+':'+ctx.add(1,2);
            }
            export function failNow(){throw Error('synchronous failure')}
            export async function versioned(input,scope){await new Promise(r=>scope.after(250,r));return 'old'}
            export function onTick(){}
            export const article={OnRequest(event){return {content:event.content,title:'group'}}};
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
            if(std::find(result.value["functions"].begin(),result.value["functions"].end(),"article.OnRequest")==result.value["functions"].end())throw std::runtime_error("Exported hook groups missing from metadata");
            std::promise<Fei::FScriptResult> grouped;auto groupedFuture=grouped.get_future();
            engine.invoke("requests","article.OnRequest",{{"content",std::string(100*1024,'x')}},[&](auto value){grouped.set_value(std::move(value));});
            if(groupedFuture.wait_for(3s)!=std::future_status::ready)throw std::runtime_error("Group invocation timed out");
            auto groupResult=groupedFuture.get();
            if(!groupResult.ok()||groupResult.value["content"].get<std::string>().size()!=100*1024||groupResult.value["title"]!="group")throw std::runtime_error("Article payload or group invocation failed");
            bool late=false;try{engine.bind("late",add);}catch(const std::logic_error&){late=true;}
            if(!late)throw std::runtime_error("Late native registration accepted");
            std::promise<Fei::FScriptResult> invalidAsync;auto invalidAsyncFuture=invalidAsync.get_future();
            engine.load("invalid",{{{"main.mjs","ctx.db.echo(1);"}},"main.mjs"},[&](auto value){invalidAsync.set_value(std::move(value));});
            if(invalidAsyncFuture.wait_for(3s)!=std::future_status::ready||invalidAsyncFuture.get().ok()||nativeCalls!=0)throw std::runtime_error("Validation performed asynchronous native work");
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
            std::promise<Fei::FScriptResult> oldCall,newVersion,newCall;
            auto oldFuture=oldCall.get_future(),versionFuture=newVersion.get_future(),newFuture=newCall.get_future();
            engine.invoke("requests","versioned",nullptr,[&](auto value){oldCall.set_value(std::move(value));});
            engine.load("requests",{{{"main.mjs","export function versioned(){return 'new'} export async function onStop(){await new Promise(r=>ctx.after(25,r));throw Error('stop failure')}"}},"main.mjs"},[&](auto value){newVersion.set_value(std::move(value));});
            if(versionFuture.wait_for(3s)!=std::future_status::ready||!versionFuture.get().ok())throw std::runtime_error("Hot update failed");
            engine.invoke("requests","versioned",nullptr,[&](auto value){newCall.set_value(std::move(value));});
            if(newFuture.wait_for(3s)!=std::future_status::ready||newFuture.get().value!="new")throw std::runtime_error("New invocation used retired code");
            if(oldFuture.wait_for(3s)!=std::future_status::ready)throw std::runtime_error("Retired invocation did not drain");
            auto oldResult=oldFuture.get();
            if(!oldResult.ok()||oldResult.value!="old"||profiledVersions!=1)throw std::runtime_error("Hot update cancelled admitted work or lost profiling version");
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';status=1;}
        std::promise<void> closed;auto closedFuture=closed.get_future();
        engine.shutdown([&]{closed.set_value();});
        if(closedFuture.wait_for(8s)!=std::future_status::ready){std::cerr<<"Engine did not drain\n";status=1;}
        else closedFuture.get();
        if(stopFailures!=1||stopRuntimeFailures!=1){std::cerr<<"Stop timing or failure observation missing\n";status=1;}
        if(profilerStarted){
            Fei::FProfiler::flush();bool found=false;
            for(const auto& row:Fei::FProfiler::snapshot().rows)if(row.label.script=="requests"&&row.label.name=="stop"&&row.errors==1&&row.maxNs>=20000000)found=true;
            if(!found){std::cerr<<"Stop profiling was not saved\n";status=1;}
        }
        loop.Quit();worker.join();
        if(!error.empty()){std::cerr<<error<<'\n';status=1;}
    }
    Fei::FProfiler::stop();delete Fei::Logger::instance();Fei::FeiUnInit();return status;
}
