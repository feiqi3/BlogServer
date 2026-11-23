#include "ScriptRuntime.h"
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <iostream>

using namespace std::chrono_literals;
using Fei::Script::ScriptRuntime;
int main() {
    try {
        ScriptRuntime runtime;
        runtime.evaluate("globalThis.answer=0; Promise.resolve().then(()=>answer=42)", 1s);
        runtime.evaluate("if(answer !== 42) throw Error('Promise did not run')", 1s);
        bool syntax = false;
        try { runtime.evaluate("function {", 1s); } catch (const std::runtime_error&) { syntax = true; }
        if (!syntax) throw std::runtime_error("syntax error not reported");
        bool interrupted = false;
        try { runtime.evaluate("while(true) {}", 30ms); } catch (const std::runtime_error&) { interrupted = true; }
        if (!interrupted) throw std::runtime_error("loop not interrupted");
        runtime.evaluate("if (1+1 !== 2) throw Error('bad')", 1s);
        bool memoryLimited = false;
        try { runtime.evaluate("let a=[]; while(true) a.push('x'.repeat(4096));", 1s); }
        catch (const std::runtime_error& e) { memoryLimited = std::string(e.what()).find("out of memory") != std::string::npos; }
        if (!memoryLimited) throw std::runtime_error("memory limit not enforced");
        bool wrongThread = false;
        std::thread other([&] { try { runtime.evaluate("1", 1s); } catch (const std::logic_error&) { wrongThread = true; } });
        other.join();
        if (!wrongThread) throw std::runtime_error("cross-thread use accepted");
        {
            ScriptRuntime modules;
            std::map<std::string, std::string> sources{{"entry.mjs", "import {value} from './dep.js'; export const answer=value+1;"}, {"dep.js", "export const value=41;"}};
            modules.loadModule(sources, "entry.mjs", 1s);
            sources["dep.js"] = "export const value=9;";
            JSValue space = modules.lifecycleObject();
            JSValue answer = JS_GetPropertyStr(modules.context(), space, "answer");
            int number = 0;
            JS_ToInt32(modules.context(), &number, answer);
            JS_FreeValue(modules.context(), answer); JS_FreeValue(modules.context(), space);
            if (number != 42) throw std::runtime_error("module dependency failed");
            modules.evaluate("globalThis.loaded=0; import('./dep.js').then(m=>loaded=m.value)", 1s);
            modules.evaluate("if(loaded!==41) throw Error('version snapshot changed')", 1s);
            ScriptRuntime nextVersion;
            nextVersion.loadModule(sources, "entry.mjs", 1s);
            space = nextVersion.lifecycleObject();
            answer = JS_GetPropertyStr(nextVersion.context(), space, "answer");
            JS_ToInt32(nextVersion.context(), &number, answer);
            JS_FreeValue(nextVersion.context(), answer); JS_FreeValue(nextVersion.context(), space);
            if (number != 10) throw std::runtime_error("new module version not loaded");
        }
        {
            ScriptRuntime forbidden;
            bool blocked = false;
            try { forbidden.loadModule({{"entry.mjs", "import '../outside.js';"}}, "entry.mjs", 1s); }
            catch (const std::runtime_error&) { blocked = true; }
            if (!blocked) throw std::runtime_error("module escape accepted");
        }
        {
            ScriptRuntime rejections;
            rejections.evaluate("Promise.reject(Error('caught')).catch(()=>{})", 1s);
            rejections.checkUnhandled();
            rejections.evaluate("Promise.reject(Error('detached'))", 1s);
            bool found = false;
            try { rejections.checkUnhandled(); } catch (const std::runtime_error&) { found = true; }
            if (!found) throw std::runtime_error("unhandled rejection lost");
        }
        for (const char* source : {"import 'std';", "await new Promise(()=>{});"}) {
            ScriptRuntime unsupported;
            bool rejected = false;
            try { unsupported.loadModule({{"entry.mjs", source}}, "entry.mjs", 1s); }
            catch (const std::runtime_error&) { rejected = true; }
            if (!rejected) throw std::runtime_error("unsupported module was accepted");
        }
        {
            ScriptRuntime jobs;
            bool stopped = false;
            try { jobs.evaluate("function again(){Promise.resolve().then(again)}; again()", 30ms); }
            catch (const std::runtime_error&) { stopped = true; }
            if (!stopped) throw std::runtime_error("unbounded Promise jobs not stopped");
        }
        std::cout << "Script runtime tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
