#include "FeiHttpBinding.h"
#include "NetworkPolicy.h"
#include "FEPollListener.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include "FSocket.h"
#include "Http/FHttpClient.h"
#include <future>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    for (const char* address : {"127.0.0.1", "10.0.0.1", "169.254.169.254", "172.16.0.1", "192.168.0.1", "100.64.0.1", "198.18.0.1", "224.0.0.1", "::1", "::ffff:127.0.0.1", "fc00::1", "fe80::1", "2001:db8::1", "2002:7f00:1::", "bad"})
        if (Fei::Script::allowPublicAddress("test", address)) return 3;
    for (const char* address : {"8.8.8.8", "1.1.1.1", "2606:4700:4700::1111"})
        if (!Fei::Script::allowPublicAddress("test", address)) return 4;
    Fei::FeiInit();
    Fei::LoggerConfig config;
    config.showLvl = Fei::lvl::warn;
    config.saveLvl = Fei::lvl::warn;
    config.saveFileName = "script-binding-test.log";
    new Fei::Logger(config);
    int status = 0;
    {
        Fei::FEventLoop loop(std::make_unique<Fei::FEPollListener>());
        std::unique_ptr<Fei::Http::FHttpClient> client;
        std::unique_ptr<Fei::Http::FHttpClient> restricted;
        bool denied = false, dnsDenied = false;
        std::shared_ptr<Fei::Script::FeiHttpBinding> binding;
        std::shared_ptr<Fei::Script::FeiHttpBinding> faulted;
        bool faultDrained = false;
        int faults = 0;
        std::promise<std::string> finished;
        auto future = finished.get_future();
        std::string error;
        const std::string url = std::string("http://127.0.0.1:") + argv[1];
        loop.AddTask([&] {
            try {
                client = std::make_unique<Fei::Http::FHttpClient>(loop);
                Fei::Http::ClientOptions policy;
                policy.allowAddress = Fei::Script::allowPublicAddress;
                restricted = std::make_unique<Fei::Http::FHttpClient>(loop, policy);
                Fei::Http::ClientRequest blocked; blocked.url = url + "/denied";
                restricted->submit(blocked, [&](auto r) { denied = !r.ok() && r.error.code == Fei::Async::ErrorCode::AccessDenied; });
                blocked.url = std::string("http://localhost:") + argv[1] + "/denied";
                restricted->submit(blocked, [&](auto r) { dnsDenied = !r.ok() && r.error.code == Fei::Async::ErrorCode::AccessDenied; });
                binding = Fei::Script::FeiHttpBinding::create(*client, [&](const std::string& e) { error = e; });
                binding->evaluate("let invalidUrls=0; for(const url of ['x'.repeat(8193),'http://example.com/\\0hidden']){try{ctx.http.get(url)}catch(e){invalidUrls++}} if(invalidUrls!==2) throw Error('URL limits failed')");
                faulted = Fei::Script::FeiHttpBinding::create(*client, [&](const std::string&) { ++faults; });
                bool rejected = false;
                try { faulted->evaluate("ctx.after(100,()=>{throw Error('cancelled timer ran')}); ctx.http.get('" + url + "/slow'); Promise.reject(Error('detached'))"); }
                catch (const std::runtime_error&) { rejected = true; }
                if (!rejected) throw std::runtime_error("Detached rejection not detected");
                faulted->close([&] { faultDrained = true; faulted.reset(); });
                binding->evaluate("globalThis.good=false; globalThis.bad=false; globalThis.timed=false; ctx.after(30,async()=>{let r=await ctx.http.get('" + url + "/ok'); timed=r.body==='hello'}); (async()=>{let r=await ctx.http.get('" + url + "/ok'); good=r.status===200&&r.body==='hello'})(); (async()=>{try {await ctx.http.get('invalid-url')} catch(e) {bad=true}})()");
                loop.RunAfter(1000, [&] {
                    try {
                        if (!error.empty()) throw std::runtime_error(error);
                        if (!denied || !dnsDenied) throw std::runtime_error("IP or DNS policy bypassed");
                        if (!faultDrained || faults != 1) throw std::runtime_error("Runtime failure did not drain cleanly");
                        binding->evaluate("if(!good || !bad || !timed) throw Error('HTTP result, rejection or timer missing')");
                        binding->evaluate("ctx.http.get('" + url + "/slow').catch(()=>{}); ctx.after(200,()=>{throw Error('cancelled timer fired')})");
                        loop.RunAfter(100, [&] {
                            binding->close([&] {
                                client->shutdown([&] {
                                    binding.reset();
                                    client.reset();
                                    restricted->shutdown([&] {
                                        restricted.reset();
                                        loop.RunAfter(250, [&] { finished.set_value(error); loop.Quit(); });
                                    });
                                });
                            });
                        });
                    } catch (const std::exception& e) { finished.set_value(e.what()); loop.Quit(); }
                });
            } catch (const std::exception& e) { finished.set_value(e.what()); loop.Quit(); }
        });
        std::thread worker([&] { loop.Loop(); });
        const auto result = future.get();
        worker.join();
        if (!result.empty()) { std::cerr << result << '\n'; status = 1; }
    }
    delete Fei::Logger::instance();
    Fei::FeiUnInit();
    return status;
}
