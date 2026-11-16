#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <map>
#include <vector>
#include "quickjs.h"

struct JSRuntime;
struct JSContext;

namespace Fei::Script {

// Owned and used exclusively by the creating thread.
class ScriptRuntime {
public:
    explicit ScriptRuntime(std::size_t memoryLimit = 8 * 1024 * 1024);
    ~ScriptRuntime();
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Evaluates global code and drains its pending Promise jobs within the
    // same time budget. Throws std::runtime_error on a script exception.
    void evaluate(std::string_view source, std::chrono::milliseconds timeout);
    JSContext* context() const noexcept { return context_; }
    // The callback and all resulting Promise jobs run within one budget.
    void resume(std::chrono::milliseconds timeout, const std::function<void()>& callback);
    void loadModule(std::map<std::string, std::string> sources, const std::string& entry,
                    std::chrono::milliseconds timeout);
    JSValue lifecycleObject() const;
    JSValue function(const std::string& path,JSValue& receiver) const;
    std::vector<std::string> functionNames();
    void watchPromise(JSValue value);
    void unwatchPromise(JSValue value);
    void checkUnhandled();
    std::string consumeException() { return takeException(context_); }

private:
    static int interrupt(JSRuntime*, void* opaque);
    std::string takeException(JSContext* context);
    void drainJobs();
    static char* normalizeModule(JSContext*, const char*, const char*, void*);
    static JSModuleDef* loadDependency(JSContext*, const char*, void*);
    static void rejection(JSContext*, JSValueConst, JSValueConst, JS_BOOL, void*);
    JSRuntime* runtime_ = nullptr;
    JSContext* context_ = nullptr;
    std::thread::id owner_;
    std::chrono::steady_clock::time_point deadline_;
    std::map<std::string, std::string> sources_;
    JSValue moduleNamespace_ = JS_UNDEFINED;
    std::map<void*, std::pair<JSValue, JSValue>> rejections_;
    std::map<void*, JSValue> observed_;
    bool rejectionOverflow_ = false;
};
}
