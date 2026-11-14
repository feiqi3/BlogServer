#pragma once
#include "ScriptRuntime.h"
#include "ScriptTypes.h"
#include "Script/FScriptEngine.h"
#include "Async/FAsyncTypes.h"
#include "Http/FHttpClientTypes.h"
#include "quickjs.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Fei::Http { class FHttpClient; struct ClientResponse; }

namespace Fei::Script {
// Prototype: construct, use and release on client.loop(); keep the loop and
// client alive until close() completion. Not a sandbox for untrusted scripts.
class FeiHttpBinding : public std::enable_shared_from_this<FeiHttpBinding> {
public:
    using ErrorHandler = std::function<void(const std::string&)>;
    static std::shared_ptr<FeiHttpBinding> create(Fei::Http::FHttpClient& client, ErrorHandler onError);
    static std::shared_ptr<FeiHttpBinding> createBare(Fei::Http::FHttpClient&, ErrorHandler);
    void installHttp();
    void installTimers();
    void installNative(const std::function<void(JSContext*, JSValueConst)>& installer);
    void bindFunction(const std::string&,FScriptEngine::NativeFunction);
    Async::OperationHandle invoke(const std::string& name, const std::string& inputJson,
        InvocationCallback completion, std::chrono::milliseconds timeout = std::chrono::seconds(30), std::uint64_t id = 0);
    void evaluate(const std::string& source);
    void loadModule(std::map<std::string, std::string> sources, const std::string& entry);
    // Optional global lifecycle function; waits for a returned Promise without blocking.
    void callHook(const std::string& name, std::function<void(std::string)> completion);
    void stopGracefully(std::function<void()> onClosed);
    void close(std::function<void()> onClosed);
    void setValidating(bool value) { checkLoop(); validating_ = value; }
private:
    explicit FeiHttpBinding(Fei::Http::FHttpClient& client, ErrorHandler onError);
    struct Pending;
    struct Timer;
    struct Hook;
    Async::OperationHandle invokeImpl(const std::string&, const std::string&, InvocationCallback,
        std::chrono::milliseconds, bool optional, bool stoppingHook, std::uint64_t id = 0);
    void pollHook(std::uint64_t id);
    void finishHook(std::uint64_t id, InvocationResult result);
    void cancelScope(std::uint64_t id);
    JSValue makeScope(std::uint64_t id);
    static JSValue scopedHttpGet(JSContext*, JSValueConst, int, JSValueConst*, int, JSValue*);
    static JSValue scopedAfter(JSContext*, JSValueConst, int, JSValueConst*, int, JSValue*);
    static JSValue httpGetImpl(JSContext*, int, JSValueConst*, std::uint64_t);
    static JSValue afterImpl(JSContext*, int, JSValueConst*, std::uint64_t);
    static JSValue httpGet(JSContext*, JSValueConst, int, JSValueConst*);
    static JSValue after(JSContext*, JSValueConst, int, JSValueConst*);
    static JSValue nativeCall(JSContext*,JSValueConst,int,JSValueConst*,int,JSValue*);
    void fireTimer(std::size_t id);
    void complete(std::shared_ptr<Pending>, Fei::Async::Result<Fei::Http::ClientResponse>);
    void checkLoop() const;
    void fault(const std::string& error);
    Fei::Http::FHttpClient& client_;
    ScriptRuntime runtime_;
    ErrorHandler onError_;
    std::unordered_map<std::size_t, std::shared_ptr<Pending>> pending_;
    std::unordered_map<std::size_t, std::shared_ptr<Timer>> timers_;
    std::size_t nextId_ = 0;
    bool closing_ = false;
    bool validating_ = false;
    bool draining_ = false;
    bool hasExecuted_ = false;
    bool httpEnabled_ = false;
    bool timersEnabled_ = false;
    std::string failure_;
    std::function<void()> onClosed_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Hook>> hooks_;
    std::vector<FScriptEngine::NativeFunction> nativeFunctions_;
};
}
