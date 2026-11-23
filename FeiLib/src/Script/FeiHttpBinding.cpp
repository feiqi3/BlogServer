#include "FeiHttpBinding.h"
#include "FEventLoop.h"
#include "Http/FHttpClient.h"
#include <cmath>
#include <stdexcept>
#include <utility>
#include <algorithm>

namespace Fei::Script {
using namespace std::chrono_literals;
struct FeiHttpBinding::Pending {
    std::size_t id;
    std::uint64_t owner = 0;
    bool binary = false;
    std::size_t reserved = 0;
    JSValue resolve = JS_UNDEFINED;
    JSValue reject = JS_UNDEFINED;
    Fei::Async::OperationHandle handle;
};
struct FeiHttpBinding::Timer {
    Fei::TimerID loopId = 0;
    std::uint64_t owner = 0;
    JSValue callback = JS_UNDEFINED;
};
struct FeiHttpBinding::Hook {
    std::string name;
    JSValue value = JS_UNDEFINED;
    Fei::TimerID timer = 0;
    std::chrono::steady_clock::time_point deadline;
    InvocationCallback completion;
    bool lifecycle=false;
    std::shared_ptr<std::atomic_bool> done = std::make_shared<std::atomic_bool>(false);
};

namespace {
InvocationResult failed(Async::ErrorCode code, std::string message) {
    return {"null", {code, std::move(message)}};
}
bool fatalMessage(const std::string& message) {
    return message.find("interrupted") != std::string::npos || message.find("out of memory") != std::string::npos;
}
bool put(JSContext* ctx,JSValueConst object,const char* name,JSValue value) {
    return !JS_IsException(value) && JS_SetPropertyStr(ctx,object,name,value) >= 0;
}
}
Async::OperationHandle FeiHttpBinding::invoke(const std::string& name, const std::string& input,
    InvocationCallback completion, std::chrono::milliseconds timeout, std::uint64_t id) {
    return invokeImpl(name, input, std::move(completion), timeout, false, false, id);
}
Async::OperationHandle FeiHttpBinding::invokeImpl(const std::string& name, const std::string& input,
    InvocationCallback completion, std::chrono::milliseconds timeout, bool optional, bool stoppingHook, std::uint64_t id) {
    checkLoop();
    if (!id) id = Async::nextOperationId();
    auto hook = std::make_shared<Hook>();
    hook->name = name;
    hook->completion = std::move(completion);
    hook->lifecycle = optional;
    auto reject = [&](Async::ErrorCode code, const std::string& message) {
        hook->done->store(true);
        if (hook->completion) hook->completion(failed(code, message));
        return Async::OperationHandle{id, {}, hook->done};
    };
    if (closing_ || (draining_ && !stoppingHook) || !failure_.empty()) return reject(Async::ErrorCode::Stopped, "Script unavailable");
    if (timeout.count() <= 0 || input.size() > maxJsonBytes_ || hooks_.contains(id)) return reject(Async::ErrorCode::InvalidArgument, "Invalid invocation");
    if (hooks_.size() >= 64) return reject(Async::ErrorCode::QueueFull, "Too many script invocations");
    hook->deadline = std::chrono::steady_clock::now() + timeout;
    hooks_.emplace(id, hook);
    std::weak_ptr<FeiHttpBinding> weak = shared_from_this();
    Async::OperationHandle handle{id, [weak, id] {
        if (auto owner = weak.lock()) owner->client_.loop().AddTask([owner, id] {
            owner->finishHook(id, failed(Async::ErrorCode::Cancelled, "Invocation cancelled"));
        });
    }, hook->done};
    try {
        std::string error;
        Async::ErrorCode code = Async::ErrorCode::UserException;
        runtime_.resume(std::min(timeout, 100ms), [&] {
            auto* ctx = runtime_.context();
            JSValue object = JS_UNDEFINED;
            JSValue function = runtime_.function(name,object);
            if (JS_IsException(function)) { JS_FreeValue(ctx,object);error = runtime_.consumeException(); return; }
            if (JS_IsUndefined(function) && optional) { JS_FreeValue(ctx, function);JS_FreeValue(ctx,object); return; }
            if (!JS_IsFunction(ctx, function)) {
                JS_FreeValue(ctx, function);JS_FreeValue(ctx,object); code = Async::ErrorCode::InvalidArgument; error = "Hook is not a function"; return;
            }
            JSValue args[2] = {JS_ParseJSON(ctx, input.c_str(), input.size(), "invocation.json"), JS_UNDEFINED};
            if (JS_IsException(args[0])) {
                error = runtime_.consumeException(); code = Async::ErrorCode::InvalidArgument;
            } else {
                args[1] = makeScope(id);
                if (JS_IsException(args[1])) error = runtime_.consumeException();
                else {
                    hook->value = JS_Call(ctx, function, object, 2, args);
                    if (JS_IsException(hook->value)) { error = runtime_.consumeException(); hook->value = JS_UNDEFINED; }
                }
            }
            if (!JS_IsException(args[0])) JS_FreeValue(ctx, args[0]);
            if (!JS_IsException(args[1])) JS_FreeValue(ctx, args[1]);
            JS_FreeValue(ctx, function);JS_FreeValue(ctx,object);
        });
        runtime_.watchPromise(hook->value);
        runtime_.checkUnhandled();
        if (!error.empty()) {
            if (fatalMessage(error)) fault(error); else finishHook(id, failed(code, error));
        } else pollHook(id);
    } catch (const std::exception& e) { fault(e.what()); }
    return handle;
}
void FeiHttpBinding::callHook(const std::string& name, std::function<void(std::string)> completion) {
    checkLoop();
    if (std::any_of(hooks_.begin(), hooks_.end(), [](const auto& entry){ return entry.second->lifecycle; })) {
        completion("Lifecycle hook already running"); return;
    }
    auto self = shared_from_this();
    invokeImpl(name, "null", [self, completion = std::move(completion)](InvocationResult result) {
        if (!result.ok() && !self->closing_ && result.error.code!=Async::ErrorCode::Cancelled && result.error.code!=Async::ErrorCode::Stopped) self->fault(result.error.message);
        completion(result.error.message);
    }, 30s, true, name == "onStop");
}
void FeiHttpBinding::pollHook(std::uint64_t id) {
    auto it = hooks_.find(id);
    if (it == hooks_.end()) return;
    auto hook = it->second;
    auto* ctx = runtime_.context();
    int state = JS_PromiseState(ctx, hook->value);
    if (state == JS_PROMISE_PENDING) {
        if (std::chrono::steady_clock::now() >= hook->deadline) {
            finishHook(id, failed(Async::ErrorCode::Timeout, "Invocation timed out")); return;
        }
        std::weak_ptr<FeiHttpBinding> weak = shared_from_this();
        hook->timer = client_.loop().RunAfter(10, [weak, id] { if (auto self = weak.lock()) self->pollHook(id); });
        return;
    }
    InvocationResult result;
    std::string error;
    try {
        runtime_.resume(100ms, [&] {
            JSValue value = state < 0 ? JS_DupValue(ctx, hook->value) : JS_PromiseResult(ctx, hook->value);
            if (state == JS_PROMISE_REJECTED) {
                const char* text = JS_ToCString(ctx, value);
                error = text ? text : "Hook Promise rejected";
                if (text) JS_FreeCString(ctx, text);
            } else if (!JS_IsUndefined(value)) {
                JSValue json = JS_JSONStringify(ctx, value, JS_UNDEFINED, JS_UNDEFINED);
                if (JS_IsException(json)) error = runtime_.consumeException();
                else if (!JS_IsUndefined(json)) {
                    size_t length = 0;
                    const char* text = JS_ToCStringLen(ctx, &length, json);
                    if (!text) error = runtime_.consumeException();
                    else {
                        if (length > maxJsonBytes_) error = "Hook result exceeds JSON budget";
                        else result.json.assign(text, length);
                        JS_FreeCString(ctx, text);
                    }
                }
                if (!JS_IsException(json)) JS_FreeValue(ctx, json);
            }
            JS_FreeValue(ctx, value);
        });
        runtime_.checkUnhandled();
        if (!error.empty()) {
            if (fatalMessage(error)) { fault(error); return; }
            result = failed(Async::ErrorCode::UserException, error);
        }
        finishHook(id, std::move(result));
    } catch (const std::exception& e) { fault(e.what()); }
}
void FeiHttpBinding::cancelScope(std::uint64_t id) {
    for(auto& [key,pending]:nativePending_)if(pending->owner==id)pending->handle.cancel();
    for (auto it = timers_.begin(); it != timers_.end();) {
        if (it->second->owner != id) { ++it; continue; }
        client_.loop().CancelTimer(it->second->loopId);
        JS_FreeValue(runtime_.context(), it->second->callback);
        it = timers_.erase(it);
    }
    for (auto& [key, pending] : pending_) if (pending->owner == id) pending->handle.cancel();
}
void FeiHttpBinding::finishHook(std::uint64_t id, InvocationResult result) {
    auto it = hooks_.find(id);
    if (it == hooks_.end()) return;
    auto hook = it->second; hooks_.erase(it);
    if (hook->timer) client_.loop().CancelTimer(hook->timer);
    cancelScope(id);
    runtime_.unwatchPromise(hook->value);
    JS_FreeValue(runtime_.context(), hook->value);
    hook->done->store(true);
    if (hook->completion) {
        try { hook->completion(std::move(result)); }
        catch (...) { /* User completion must not unwind through the loop. */ }
    }
}
void FeiHttpBinding::cancelInvocations(){
    checkLoop();std::vector<std::uint64_t> cancel;
    for(const auto& [id,hook]:hooks_)if(!hook->lifecycle)cancel.push_back(id);
    for(auto id:cancel)finishHook(id,failed(Async::ErrorCode::Stopped,"Engine stopping"));
}
void FeiHttpBinding::stopGracefully(std::function<void()> onClosed,bool cancelRequests,StopObserver observer) {
    checkLoop();
    if (!draining_) {
        draining_ = true;
    }
    if(cancelRequests)cancelInvocations();
    if (!failure_.empty()) { close(std::move(onClosed)); return; }
    auto self = shared_from_this();
    if (!hooks_.empty()) {
        client_.loop().RunAfter(10, [self, onClosed = std::move(onClosed),cancelRequests,observer=std::move(observer)]() mutable { self->stopGracefully(std::move(onClosed),cancelRequests,std::move(observer)); });
        return;
    }
    const auto begin=observer?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point();
    callHook("onStop", [self, onClosed = std::move(onClosed),observer=std::move(observer),begin](std::string error) mutable {
        if(observer){try{observer(std::move(error),std::chrono::steady_clock::now()-begin);}catch(...){}}
        self->close(std::move(onClosed));
    });
}

FeiHttpBinding::FeiHttpBinding(Fei::Http::FHttpClient& client, ErrorHandler onError,std::size_t maxJsonBytes,std::size_t memoryLimitBytes)
    : maxJsonBytes_(maxJsonBytes), client_(client), runtime_(memoryLimitBytes), onError_(std::move(onError)) { checkLoop(); }

std::shared_ptr<FeiHttpBinding> FeiHttpBinding::createBare(Fei::Http::FHttpClient& client, ErrorHandler onError,std::size_t maxJsonBytes,std::size_t memoryLimitBytes) {
    auto self = std::shared_ptr<FeiHttpBinding>(new FeiHttpBinding(client, std::move(onError),maxJsonBytes,memoryLimitBytes));
    auto* ctx = self->runtime_.context();
    JS_SetContextOpaque(ctx, self.get());
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "ctx", JS_NewObject(ctx));
    JS_FreeValue(ctx, global);
    return self;
}
std::shared_ptr<FeiHttpBinding> FeiHttpBinding::create(Fei::Http::FHttpClient& client, ErrorHandler onError,std::size_t maxJsonBytes,std::size_t memoryLimitBytes) {
    auto self = createBare(client, std::move(onError),maxJsonBytes,memoryLimitBytes);
    self->installHttp(); self->installTimers();
    return self;
}
void FeiHttpBinding::installNative(const std::function<void(JSContext*, JSValueConst)>& installer) {
    checkLoop();
    if (hasExecuted_ || closing_) throw std::logic_error("Components are immutable after execution starts");
    auto* ctx = runtime_.context();
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue host = JS_GetPropertyStr(ctx, global, "ctx");
    JS_FreeValue(ctx, global);
    try { installer(ctx, host); } catch (...) { JS_FreeValue(ctx, host); throw; }
    JS_FreeValue(ctx, host);
}
void FeiHttpBinding::installHttp() {
    if (httpEnabled_) throw std::logic_error("HTTP component already installed");
    installNative([](JSContext* ctx, JSValueConst host) {
        JSValue http = JS_NewObject(ctx);
        if (JS_IsException(http)) throw std::runtime_error("Cannot install HTTP component");
        if (!put(ctx,http,"get",JS_NewCFunction(ctx,httpGet,"get",1))) { JS_FreeValue(ctx,http); throw std::runtime_error("Cannot install HTTP component"); }
        if (!put(ctx,host,"http",http)) throw std::runtime_error("Cannot install HTTP component");
    });
    httpEnabled_ = true;
}
void FeiHttpBinding::bindFunction(const std::string& name,FScriptEngine::NativeFunction function) {
    const auto index=nativeFunctions_.size();
    nativeFunctions_.push_back(std::move(function));
    installNative([&](JSContext* ctx,JSValueConst host) {
        JSValue data=JS_NewInt64(ctx,index);
        JSValue value=JS_NewCFunctionData(ctx,nativeCall,0,0,1,&data);
        JS_FreeValue(ctx,data);
        if(!put(ctx,host,name.c_str(),value))throw std::runtime_error("Native binding registration failed");
    });
}
JSValue FeiHttpBinding::nativeCall(JSContext* ctx,JSValueConst,int argc,JSValueConst* argv,int,JSValue* data) {
    auto* self=static_cast<FeiHttpBinding*>(JS_GetContextOpaque(ctx));
    if(self->validating_||self->closing_||!self->failure_.empty())return JS_ThrowInternalError(ctx,"Native functions unavailable in this phase");
    int64_t index=0;
    if(JS_ToInt64(ctx,&index,data[0])<0)return JS_EXCEPTION;
    JSValue array=JS_NewArray(ctx);
    if(JS_IsException(array))return array;
    for(int i=0;i<argc;++i) {
        if(JS_SetPropertyUint32(ctx,array,i,JS_DupValue(ctx,argv[i]))<0){JS_FreeValue(ctx,array);return JS_EXCEPTION;}
    }
    JSValue json=JS_JSONStringify(ctx,array,JS_UNDEFINED,JS_UNDEFINED); JS_FreeValue(ctx,array);
    if(JS_IsException(json))return json;
    size_t length=0;const char* text=JS_ToCStringLen(ctx,&length,json);
    if(!text){JS_FreeValue(ctx,json);return JS_EXCEPTION;}
    try {
        if(length>self->maxJsonBytes_)throw std::invalid_argument("Native arguments exceed JSON budget");
        auto args=nlohmann::json::parse(text,text+length);
        JS_FreeCString(ctx,text);text=nullptr;JS_FreeValue(ctx,json);json=JS_UNDEFINED;
        auto result=self->nativeFunctions_.at(static_cast<std::size_t>(index))(args).dump();
        if(result.size()>self->maxJsonBytes_)throw std::invalid_argument("Native result exceeds JSON budget");
        return JS_ParseJSON(ctx,result.c_str(),result.size(),"native-result.json");
    }catch(const std::exception& e){
        if(text)JS_FreeCString(ctx,text);JS_FreeValue(ctx,json);
        return JS_ThrowTypeError(ctx,"%s",e.what());
    }catch(...){
        if(text)JS_FreeCString(ctx,text);JS_FreeValue(ctx,json);
        return JS_ThrowInternalError(ctx,"Native function failed");
    }
}
void FeiHttpBinding::installTimers() {
    if (timersEnabled_) throw std::logic_error("Timer component already installed");
    installNative([](JSContext* ctx, JSValueConst host) {
        if (!put(ctx,host,"after",JS_NewCFunction(ctx,after,"after",2))) throw std::runtime_error("Cannot install timer component");
    });
    timersEnabled_ = true;
}
JSValue FeiHttpBinding::makeScope(std::uint64_t id) {
    auto* ctx = runtime_.context();
    JSValue scope = JS_NewObject(ctx);
    if (JS_IsException(scope)) return scope;
    JSValue data = JS_NewInt64(ctx, id);
    if (!put(ctx,scope,"id",JS_DupValue(ctx,data))) { JS_FreeValue(ctx,scope); JS_FreeValue(ctx,data); return JS_EXCEPTION; }
    if (httpEnabled_) {
        JSValue http = JS_NewObject(ctx);
        if (JS_IsException(http) || !put(ctx,http,"get",JS_NewCFunctionData(ctx,scopedHttpGet,1,0,1,&data))) {
            if (!JS_IsException(http)) JS_FreeValue(ctx,http);
            JS_FreeValue(ctx,scope); JS_FreeValue(ctx,data); return JS_EXCEPTION;
        }
        if (!put(ctx,scope,"http",http)) { JS_FreeValue(ctx,scope); JS_FreeValue(ctx,data); return JS_EXCEPTION; }
    }
    if (timersEnabled_ && !put(ctx,scope,"after",JS_NewCFunctionData(ctx,scopedAfter,2,0,1,&data))) {
        JS_FreeValue(ctx,scope); JS_FreeValue(ctx,data); return JS_EXCEPTION;
    }
    try{for(std::size_t index=0;index<asynchronous_.size();++index)installAsync(scope,index,id);}
    catch(const std::exception& error){JS_FreeValue(ctx,scope);JS_FreeValue(ctx,data);return JS_ThrowInternalError(ctx,"%s",error.what());}
    JS_FreeValue(ctx, data);
    return scope;
}
JSValue FeiHttpBinding::scopedHttpGet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int64_t id = 0; if (JS_ToInt64(ctx, &id, data[0]) < 0) return JS_EXCEPTION;
    return httpGetImpl(ctx, argc, argv, static_cast<std::uint64_t>(id));
}
JSValue FeiHttpBinding::scopedAfter(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int, JSValue* data) {
    int64_t id = 0; if (JS_ToInt64(ctx, &id, data[0]) < 0) return JS_EXCEPTION;
    return afterImpl(ctx, argc, argv, static_cast<std::uint64_t>(id));
}
JSValue FeiHttpBinding::after(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return afterImpl(ctx, argc, argv, 0);
}
JSValue FeiHttpBinding::afterImpl(JSContext* ctx, int argc, JSValueConst* argv, std::uint64_t owner) {
    auto* self = static_cast<FeiHttpBinding*>(JS_GetContextOpaque(ctx));
    if (owner && !self->hooks_.contains(owner)) return JS_ThrowInternalError(ctx, "Invocation has ended");
    if (self->validating_) return JS_ThrowInternalError(ctx, "Host operations are disabled during validation");
    if (self->closing_ || !self->failure_.empty()) return JS_ThrowInternalError(ctx, "Script is stopping");
    if (argc != 2 || !JS_IsNumber(argv[0]) || !JS_IsFunction(ctx, argv[1]))
        return JS_ThrowTypeError(ctx, "expected delay in milliseconds and callback");
    double delay;
    if (JS_ToFloat64(ctx, &delay, argv[0]) < 0) return JS_EXCEPTION;
    if (!std::isfinite(delay) || std::trunc(delay) != delay || delay < 0 || delay > 86400000)
        return JS_ThrowRangeError(ctx, "delay must be between 0 and 86400000 ms");
    if (self->timers_.size() >= 64) return JS_ThrowInternalError(ctx, "Too many timers");
    auto timer = std::make_shared<Timer>();
    timer->owner = owner;
    timer->callback = JS_DupValue(ctx, argv[1]);
    const auto id = ++self->nextId_;
    self->timers_.emplace(id, timer);
    std::weak_ptr<FeiHttpBinding> weak = self->shared_from_this();
    timer->loopId = self->client_.loop().RunAfter(static_cast<Fei::uint64>(delay), [weak, id] {
        if (auto owner = weak.lock()) owner->fireTimer(id);
    });
    return JS_UNDEFINED;
}
void FeiHttpBinding::fireTimer(std::size_t id) {
    checkLoop();
    auto it = timers_.find(id);
    if (it == timers_.end()) return;
    auto timer = it->second;
    timers_.erase(it);
    JSContext* ctx = runtime_.context();
    try {
        runtime_.resume(100ms, [&] {
            JSValue result = JS_Call(ctx, timer->callback, JS_UNDEFINED, 0, nullptr);
            if (JS_IsException(result)) {
                JSValue exception = JS_GetException(ctx);
                const char* message = JS_ToCString(ctx, exception);
                std::string error = message ? message : "Timer callback failed";
                if (message) JS_FreeCString(ctx, message);
                JS_FreeValue(ctx, exception);
                throw std::runtime_error(error);
            }
            JS_FreeValue(ctx, result);
        });
        runtime_.checkUnhandled();
    } catch (const std::exception& e) {
        fault(e.what());
    }
    JS_FreeValue(ctx, timer->callback);
}
void FeiHttpBinding::checkLoop() const {
    if (!client_.loop().isInLoopThread()) throw std::logic_error("Script binding must run on the client loop");
}
std::vector<std::string> FeiHttpBinding::functionNames(){checkLoop();return runtime_.functionNames();}
void FeiHttpBinding::evaluate(const std::string& source) {
    checkLoop();
    if (closing_ || !failure_.empty()) throw std::logic_error("Script binding is unavailable");
    hasExecuted_ = true;
    try { runtime_.evaluate(source, 100ms); runtime_.checkUnhandled(); }
    catch (const std::exception& e) { fault(e.what()); throw; }
}
void FeiHttpBinding::loadModule(std::map<std::string, std::string> sources, const std::string& entry) {
    checkLoop();
    if (closing_ || !failure_.empty()) throw std::logic_error("Script unavailable");
    hasExecuted_ = true;
    try { runtime_.loadModule(std::move(sources), entry, 100ms); }
    catch (const std::exception& e) { fault(e.what()); throw; }
}
void FeiHttpBinding::fault(const std::string& error) {
    if (!failure_.empty()) return;
    failure_ = error;
    for (auto& [id, timer] : timers_) {
        client_.loop().CancelTimer(timer->loopId);
        JS_FreeValue(runtime_.context(), timer->callback);
    }
    timers_.clear();
    for (auto& [id, pending] : pending_) pending->handle.cancel();
    for(auto& [id,pending]:nativePending_)pending->handle.cancel();
    if (onError_) onError_(error);
    while (!hooks_.empty()) finishHook(hooks_.begin()->first, failed(Async::ErrorCode::UserException, error));
}
JSValue FeiHttpBinding::httpGet(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return httpGetImpl(ctx, argc, argv, 0);
}
JSValue FeiHttpBinding::httpGetImpl(JSContext* ctx, int argc, JSValueConst* argv, std::uint64_t owner) {
    auto* self = static_cast<FeiHttpBinding*>(JS_GetContextOpaque(ctx));
    if (owner && !self->hooks_.contains(owner)) return JS_ThrowInternalError(ctx, "Invocation has ended");
    if (self->validating_) return JS_ThrowInternalError(ctx, "Host operations are disabled during validation");
    if (self->closing_ || !self->failure_.empty()) return JS_ThrowInternalError(ctx, "Script is stopping");
    if (argc < 1 || !JS_IsString(argv[0])) return JS_ThrowTypeError(ctx, "URL must be a string");
    if (self->pending_.size() >= 64) return JS_ThrowInternalError(ctx, "Too many requests");
    size_t urlLength = 0;
    const char* url = JS_ToCStringLen(ctx, &urlLength, argv[0]);
    if (!url) return JS_EXCEPTION;
    if (urlLength > 8192 || std::char_traits<char>::length(url) != urlLength) {
        JS_FreeCString(ctx, url);
        return JS_ThrowRangeError(ctx, "URL must be at most 8192 bytes and contain no NUL");
    }
    Fei::Http::ClientRequest request;
    try{request.url.assign(url,urlLength);}
    catch(const std::bad_alloc&){JS_FreeCString(ctx,url);return JS_ThrowOutOfMemory(ctx);}
    JS_FreeCString(ctx, url);
    request.timeout = 5s;
    request.maxBodyBytes = 1024 * 1024;
    bool binary=false;
    try{
        if(!httpOptions(ctx,argc>1?argv[1]:JS_UNDEFINED,request,binary))return JS_EXCEPTION;
    }catch(const std::bad_alloc&){return JS_ThrowOutOfMemory(ctx);}
    catch(...){return JS_ThrowInternalError(ctx,"Cannot configure HTTP request");}
    constexpr std::size_t binaryBudget=8*1024*1024;
    if(binary&&request.maxBodyBytes>binaryBudget-self->binaryReserved_)
        return JS_ThrowRangeError(ctx,"Binary requests exceed the 8 MiB in-flight budget");
    std::shared_ptr<Pending> pending;
    try{pending=std::make_shared<Pending>();}
    catch(const std::bad_alloc&){return JS_ThrowOutOfMemory(ctx);}
    pending->id = ++self->nextId_;
    pending->owner = owner;
    pending->binary=binary;
    JSValue functions[2];
    JSValue promise = JS_NewPromiseCapability(ctx, functions);
    if (JS_IsException(promise)) return promise;
    pending->resolve = functions[0];
    pending->reject = functions[1];
    try{self->pending_.emplace(pending->id, pending);}
    catch(const std::bad_alloc&){
        JS_FreeValue(ctx,pending->resolve);JS_FreeValue(ctx,pending->reject);JS_FreeValue(ctx,promise);
        return JS_ThrowOutOfMemory(ctx);
    }
    if(binary){pending->reserved=request.maxBodyBytes;self->binaryReserved_+=pending->reserved;}
    auto keep = self->shared_from_this();
    try{
        pending->handle = self->client_.submit(std::move(request), [keep, pending](auto result) {
            keep->complete(pending, std::move(result));
        });
    }catch(...){
        self->binaryReserved_-=pending->reserved;self->pending_.erase(pending->id);
        JS_FreeValue(ctx,pending->resolve);JS_FreeValue(ctx,pending->reject);JS_FreeValue(ctx,promise);
        return JS_ThrowInternalError(ctx,"Cannot submit HTTP request");
    }
    return promise;
}
void FeiHttpBinding::complete(std::shared_ptr<Pending> pending, Fei::Async::Result<Fei::Http::ClientResponse> result) {
    checkLoop();
    auto* ctx = runtime_.context();
    // Release network admission before resolving: continuations can start a new
    // bounded request immediately. ArrayBuffer storage has its own VM heap limit.
    binaryReserved_-=pending->reserved;pending->reserved=0;
    if (closing_) {
        JS_FreeValue(ctx,pending->resolve);JS_FreeValue(ctx,pending->reject);pending_.erase(pending->id);
        notifyClosed();
        return;
    }
    try {
        if (!failure_.empty()) throw std::runtime_error(failure_);
        if (pending->owner && !hooks_.contains(pending->owner)) {
            JS_FreeValue(ctx, pending->resolve); JS_FreeValue(ctx, pending->reject);
            pending_.erase(pending->id);
            notifyClosed();
            return;
        }
        runtime_.resume(100ms, [&] {
            JSValue argument;
            bool success=result.ok();
            if (result.ok()) {
                argument=httpResponse(ctx,*result.value,pending->binary);
            } else {
                argument = JS_NewString(ctx, result.error.message.c_str());
            }
            // In particular, a retained body may exhaust this VM's heap. Reject
            // this fetch with the actual JS exception instead of faulting others.
            if(JS_IsException(argument)){argument=JS_GetException(ctx);success=false;}
            JSValue function = success ? pending->resolve : pending->reject;
            JSValue ret = JS_Call(ctx, function, JS_UNDEFINED, 1, &argument);
            JS_FreeValue(ctx, argument);
            if (JS_IsException(ret)) {
                JSValue e = JS_GetException(ctx);
                JS_FreeValue(ctx, e);
                throw std::runtime_error("Promise completion failed");
            }
            JS_FreeValue(ctx, ret);
        });
        runtime_.checkUnhandled();
    } catch (const std::exception& e) {
        fault(e.what());
    }
    JS_FreeValue(ctx, pending->resolve);
    JS_FreeValue(ctx, pending->reject);
    pending_.erase(pending->id);
    notifyClosed();
}
void FeiHttpBinding::close(std::function<void()> onClosed) {
    checkLoop();
    if (closing_) throw std::logic_error("Script binding already closing");
    closing_ = true;
    onClosed_ = std::move(onClosed);
    while (!hooks_.empty()) finishHook(hooks_.begin()->first, failed(Async::ErrorCode::Stopped, "Script stopped"));
    for (auto& [id, timer] : timers_) {
        client_.loop().CancelTimer(timer->loopId);
        JS_FreeValue(runtime_.context(), timer->callback);
    }
    timers_.clear();
    for (auto& [id, pending] : pending_) pending->handle.cancel();
    for(auto& [id,pending]:nativePending_)pending->handle.cancel();
    notifyClosed();
}
std::string FeiHttpBinding::invocationName(std::uint64_t id)const{
    auto it=hooks_.find(id);return it==hooks_.end()?std::string():it->second->name;
}
void FeiHttpBinding::notifyClosed(){
    if (closing_ && pending_.empty() && nativePending_.empty() && onClosed_) {
        auto callback = std::move(onClosed_);
        callback();
    }
}
}
