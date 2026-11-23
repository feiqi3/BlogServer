#include "ScriptVersions.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include "FSocket.h"
#include <condition_variable>
#include <mutex>
#include <thread>
#include <fstream>
#include <iostream>
using namespace std::chrono_literals;
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Fei::FeiInit();
    Fei::LoggerConfig cfg; cfg.saveFileName="script-version-test.log";
    new Fei::Logger(cfg);
    int status=0;
    {
        Fei::FEventLoop loop(std::make_unique<Fei::FEPollListener>());
        std::thread worker([&]{loop.Loop();});
        std::mutex mutex; std::condition_variable condition; int ticks=0;
        auto reporter=[&](const Blog::Script::ExecutionResult& result) {
            if(result.phase=="tick" && result.error.empty()) {
                std::lock_guard lock(mutex); ++ticks; condition.notify_all();
            }
        };
        auto waitTicks=[&] {
            std::unique_lock lock(mutex); int wanted=ticks+2;
            if(!condition.wait_for(lock,5s,[&]{return ticks>=wanted;})) throw std::runtime_error("Active version stopped ticking");
        };
        auto policy=[](std::string_view host,std::string_view address){return host=="127.0.0.1" && address=="127.0.0.1";};
        const std::string url=argv[2];
        auto bundle=[&](std::string label) {
            return Blog::Script::ModuleBundle{{
                {"main.mjs", "import {label} from './dependency.js'; export async function onStart(){await ctx.http.get('"+url+"/'+label+'/start')} export async function onTick(){await new Promise(r=>ctx.after(50,r)); await ctx.http.get('"+url+"/'+label+'/tick')} export async function onStop(){await ctx.http.get('"+url+"/'+label+'/stop')}"},
                {"dependency.js", "export const label='"+label+"';"}}, "main.mjs"};
        };
        try {
            std::string first,second;
            {
                Blog::Script::ScriptVersions versions(loop,argv[1],20ms,reporter,policy);
                first=versions.upload(bundle("v1")); waitTicks();
                if (versions.upload(bundle("v1")) != first) throw std::runtime_error("Duplicate upload changed version identity");
                for(const char* source : {"export function {", "export async function onStart(){throw Error('bad start')}", "ctx.http.get('http://127.0.0.1/side-effect');"}) {
                    bool rejected=false;
                    try { versions.upload({{{"main.mjs", source}},"main.mjs"}); } catch(const std::exception&){rejected=true;}
                    if(!rejected || versions.active().current!=first) throw std::runtime_error("Invalid update replaced active version");
                }
                waitTicks();
                second=versions.upload(bundle("v2")); waitTicks();
                if(versions.active().previous!=first) throw std::runtime_error("History missing");
                versions.rollback(); waitTicks();
                if(versions.active().current!=first || versions.active().previous!=second) throw std::runtime_error("Rollback failed");
                versions.shutdown();
            }
            {
                Blog::Script::ScriptVersions restarted(loop,argv[1],20ms,reporter,policy);
                restarted.restore(); waitTicks();
                if(restarted.active().current!=first) throw std::runtime_error("Restart selected wrong version");
                restarted.shutdown();
            }
            Blog::Script::VersionStore store(argv[1]);
            bool blocked=false;
            try { store.save({{{"../escape.mjs",""}},"../escape.mjs"}); } catch(const std::exception&){blocked=true;}
            if(!blocked) throw std::runtime_error("Unsafe version path accepted");
            std::ofstream(std::filesystem::path(argv[1])/first/"code/dependency.js") << "export const label='tampered';";
            blocked=false;
            try { store.load(first); } catch(const std::exception&){blocked=true;}
            if(!blocked) throw std::runtime_error("Corrupted version accepted");
        } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; status=1; }
        loop.Quit(); worker.join();
    }
    delete Fei::Logger::instance(); Fei::FeiUnInit();
    return status;
}
