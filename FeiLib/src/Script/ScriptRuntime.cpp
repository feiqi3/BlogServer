#include "ScriptRuntime.h"
#include "quickjs.h"
#include <stdexcept>
#include <filesystem>

namespace Fei::Script {
ScriptRuntime::ScriptRuntime(std::size_t memoryLimit) : owner_(std::this_thread::get_id()) {
    runtime_ = JS_NewRuntime();
    if (!runtime_) throw std::runtime_error("Cannot create QuickJS runtime");
    JS_SetMemoryLimit(runtime_, memoryLimit);
    JS_SetMaxStackSize(runtime_, 512 * 1024);
    JS_SetInterruptHandler(runtime_, interrupt, this);
    context_ = JS_NewContext(runtime_);
    if (!context_) {
        JS_FreeRuntime(runtime_);
        runtime_ = nullptr;
        throw std::runtime_error("Cannot create QuickJS context");
    }
    JS_SetHostPromiseRejectionTracker(runtime_, rejection, this);
    JS_SetModuleLoaderFunc(runtime_, normalizeModule, loadDependency, this);
}
ScriptRuntime::~ScriptRuntime() {
    JS_SetHostPromiseRejectionTracker(runtime_, nullptr, nullptr);
    for (auto& [key, values] : rejections_) { JS_FreeValue(context_, values.first); JS_FreeValue(context_, values.second); }
    for (auto& [key, value] : observed_) JS_FreeValue(context_, value);
    JS_FreeValue(context_, moduleNamespace_);
    JS_FreeContext(context_);
    JS_FreeRuntime(runtime_);
}
void ScriptRuntime::rejection(JSContext* ctx, JSValueConst promise, JSValueConst reason, JS_BOOL handled, void* opaque) {
    auto* self = static_cast<ScriptRuntime*>(opaque);
    void* key = JS_VALUE_GET_PTR(promise);
    auto it = self->rejections_.find(key);
    if (it != self->rejections_.end()) {
        JS_FreeValue(ctx, it->second.first); JS_FreeValue(ctx, it->second.second);
        self->rejections_.erase(it);
    }
    if (handled || self->observed_.contains(key)) return;
    try {
        if (self->rejections_.size() >= 64) { self->rejectionOverflow_ = true; return; }
        auto [entry, inserted] = self->rejections_.emplace(key, std::pair{JS_UNDEFINED, JS_UNDEFINED});
        entry->second = {JS_DupValue(ctx, promise), JS_DupValue(ctx, reason)};
    } catch (...) { self->rejectionOverflow_ = true; }
}
void ScriptRuntime::watchPromise(JSValue value) {
    if (JS_PromiseState(context_, value) < 0) return;
    auto key = JS_VALUE_GET_PTR(value);
    if (!observed_.contains(key)) observed_.emplace(key, JS_DupValue(context_, value));
    auto it = rejections_.find(key);
    if (it != rejections_.end()) {
        JS_FreeValue(context_, it->second.first); JS_FreeValue(context_, it->second.second); rejections_.erase(it);
    }
}
void ScriptRuntime::unwatchPromise(JSValue value) {
    if (JS_PromiseState(context_, value) < 0) return;
    auto it = observed_.find(JS_VALUE_GET_PTR(value));
    if (it != observed_.end()) { JS_FreeValue(context_, it->second); observed_.erase(it); }
}
void ScriptRuntime::checkUnhandled() {
    if (rejections_.empty() && !rejectionOverflow_) return;
    for (auto& [key, values] : rejections_) { JS_FreeValue(context_, values.first); JS_FreeValue(context_, values.second); }
    rejections_.clear(); rejectionOverflow_ = false;
    throw std::runtime_error("Unhandled JavaScript Promise rejection");
}
char* ScriptRuntime::normalizeModule(JSContext* ctx, const char* base, const char* name, void*) {
    try {
        std::string specifier(name);
        if (!(specifier.starts_with("./") || specifier.starts_with("../")) || specifier.find('\\') != std::string::npos || specifier.find(':') != std::string::npos)
            throw std::runtime_error("Only relative module imports are supported");
        auto path = (std::filesystem::path(base).parent_path() / specifier).lexically_normal().generic_string();
        if (path.empty() || path == ".." || path.starts_with("../") || path.starts_with('/')) throw std::runtime_error("Module import escapes bundle");
        return js_strdup(ctx, path.c_str());
    } catch (const std::exception& e) { JS_ThrowReferenceError(ctx, "%s", e.what()); return nullptr; }
}
JSModuleDef* ScriptRuntime::loadDependency(JSContext* ctx, const char* name, void* opaque) {
    auto* self = static_cast<ScriptRuntime*>(opaque);
    auto it = self->sources_.find(name);
    if (it == self->sources_.end()) { JS_ThrowReferenceError(ctx, "Module is not in this version's bundle"); return nullptr; }
    JSValue value = JS_Eval(ctx, it->second.c_str(), it->second.size(), name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(value)) return nullptr;
    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(value));
    JS_FreeValue(ctx, value);
    return module;
}
JSValue ScriptRuntime::lifecycleObject() const {
    return JS_IsUndefined(moduleNamespace_) ? JS_GetGlobalObject(context_) : JS_DupValue(context_, moduleNamespace_);
}
void ScriptRuntime::loadModule(std::map<std::string, std::string> sources, const std::string& entry, std::chrono::milliseconds timeout) {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Script runtime used from another thread");
    if (!sources_.empty() || !JS_IsUndefined(moduleNamespace_)) throw std::logic_error("Module version already loaded");
    std::size_t total = 0;
    for (const auto& [name, source] : sources) total += source.size();
    if (sources.size() > 32 || total > 512 * 1024 || !sources.contains(entry)) throw std::invalid_argument("Invalid module bundle");
    sources_ = std::move(sources);
    resume(timeout, [&] {
        const auto& source = sources_.at(entry);
        JSValue compiled = JS_Eval(context_, source.c_str(), source.size(), entry.c_str(), JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) throw std::runtime_error(takeException(context_));
        auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
        JSValue result = JS_EvalFunction(context_, compiled);
        if (JS_IsException(result)) throw std::runtime_error(takeException(context_));
        try { drainJobs(); } catch (...) { JS_FreeValue(context_, result); throw; }
        int state = JS_PromiseState(context_, result);
        JS_FreeValue(context_, result);
        if (state == JS_PROMISE_PENDING) throw std::runtime_error("Top-level asynchronous I/O is not supported; use lifecycle hooks");
        if (state == JS_PROMISE_REJECTED) throw std::runtime_error("Module evaluation rejected");
        moduleNamespace_ = JS_GetModuleNamespace(context_, module);
        if (JS_IsException(moduleNamespace_)) { moduleNamespace_ = JS_UNDEFINED; throw std::runtime_error(takeException(context_)); }
    });
    checkUnhandled();
}
int ScriptRuntime::interrupt(JSRuntime*, void* opaque) {
    auto* self = static_cast<ScriptRuntime*>(opaque);
    return std::chrono::steady_clock::now() >= self->deadline_;
}
std::string ScriptRuntime::takeException(JSContext* context) {
    JSValue exception = JS_GetException(context);
    const char* message = JS_ToCString(context, exception);
    std::string result = message ? message : "Unknown JavaScript exception";
    if (message) JS_FreeCString(context, message);
    JS_FreeValue(context, exception);
    return result;
}
void ScriptRuntime::evaluate(std::string_view source, std::chrono::milliseconds timeout) {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Script runtime used from another thread");
    if (timeout <= std::chrono::milliseconds::zero()) throw std::invalid_argument("Timeout must be positive");
    deadline_ = std::chrono::steady_clock::now() + timeout;
    // QuickJS requires a NUL byte after the input, which string_view does not guarantee.
    std::string input(source);
    JSValue result = JS_Eval(context_, input.c_str(), input.size(), "script.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) throw std::runtime_error(takeException(context_));
    JS_FreeValue(context_, result);
    drainJobs();
}
void ScriptRuntime::resume(std::chrono::milliseconds timeout, const std::function<void()>& callback) {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Script runtime used from another thread");
    if (timeout <= std::chrono::milliseconds::zero()) throw std::invalid_argument("Timeout must be positive");
    deadline_ = std::chrono::steady_clock::now() + timeout;
    callback();
    drainJobs();
}
JSValue ScriptRuntime::function(const std::string& path,JSValue& receiver) const {
    receiver=JS_UNDEFINED;
    JSValue object=lifecycleObject();std::size_t begin=0;
    while(true){
        auto end=path.find('.',begin);auto part=path.substr(begin,end==std::string::npos?end:end-begin);
        if(part.empty()||part=="__proto__"||part=="constructor"||part=="prototype"){
            JS_FreeValue(context_,object);return JS_ThrowTypeError(context_,"Invalid function path");
        }
        auto value=JS_GetPropertyStr(context_,object,part.c_str());
        if(end==std::string::npos){
            if(begin)receiver=object;else JS_FreeValue(context_,object);
            return value;
        }
        JS_FreeValue(context_,object);
        if(JS_IsException(value))return value;
        if(!JS_IsObject(value)){JS_FreeValue(context_,value);return JS_UNDEFINED;}
        object=value;begin=end+1;
    }
}
std::vector<std::string> ScriptRuntime::functionNames(){
    std::vector<std::string> names;
    resume(std::chrono::milliseconds(100),[&]{
        struct Properties {
            JSContext* context;JSPropertyEnum* values=nullptr;uint32_t count=0;
            ~Properties(){for(uint32_t i=0;i<count;++i)JS_FreeAtom(context,values[i].atom);js_free(context,values);}
        };
        std::function<void(JSValueConst,const std::string&,int)> inspect;
        inspect=[&](JSValueConst object,const std::string& prefix,int depth){
            Properties properties{context_};
            if(JS_GetOwnPropertyNames(context_,&properties.values,&properties.count,object,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)<0)throw std::runtime_error(takeException(context_));
            if(properties.count>512)throw std::runtime_error("Too many exported properties");
            for(uint32_t i=0;i<properties.count;++i){
                const char* text=JS_AtomToCString(context_,properties.values[i].atom);
                if(!text)throw std::runtime_error(takeException(context_));
                std::string key(text);JS_FreeCString(context_,text);
                if(key=="__proto__"||key=="constructor"||key=="prototype")continue;
                auto value=JS_GetProperty(context_,object,properties.values[i].atom);
                if(JS_IsException(value))throw std::runtime_error(takeException(context_));
                try{
                    auto name=prefix+key;
                    if(JS_IsFunction(context_,value)){
                        names.push_back(std::move(name));
                        if(names.size()>512)throw std::runtime_error("Too many exported functions");
                    }else if(depth==0&&JS_IsObject(value))inspect(value,name+".",depth+1);
                }catch(...){JS_FreeValue(context_,value);throw;}
                JS_FreeValue(context_,value);
            }
        };
        auto object=lifecycleObject();
        try{inspect(object,"",0);}catch(...){JS_FreeValue(context_,object);throw;}
        JS_FreeValue(context_,object);
    });
    return names;
}
void ScriptRuntime::drainJobs() {
    JSContext* jobContext = nullptr;
    int status;
    while ((status = JS_ExecutePendingJob(runtime_, &jobContext)) > 0) {
        if (std::chrono::steady_clock::now() >= deadline_) throw std::runtime_error("Script execution timed out");
    }
    if (status < 0) throw std::runtime_error(takeException(jobContext));
}
}
