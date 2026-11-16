#pragma once
#include "FDef.h"
#include "Async/FAsyncTypes.h"
#include "Http/FHttpClient.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <limits>
#include <stdexcept>

namespace Fei {
class FEventLoop;
using ScriptValue = nlohmann::json;
struct FScriptSource { std::map<std::string,std::string> sources; std::string entry; };
struct FScriptResult {
    ScriptValue value;
    Async::Error error;
    bool ok() const { return error.code == Async::ErrorCode::None; }
};
struct FScriptExecution {
    std::string script, phase, error;
    std::chrono::milliseconds duration{};
    std::chrono::microseconds elapsed{};
    std::uint64_t generation = 0;
};
struct FScriptNativeCall {
    std::string script, hook;
    std::uint64_t invocationId = 0;
    ScriptValue arguments;
};
struct FScriptOptions {
    Http::ClientOptions http;
    std::function<void(const FScriptExecution&)> report;
    std::size_t maxJsonBytes = 64 * 1024;
    bool profileInvocations = false;
    bool reportInvocations = false; // Optional string-based observer; profiling itself uses cached IDs.
};
namespace ScriptDetail {
template<class T> T argument(const ScriptValue& value) {
    if constexpr (std::is_same_v<T,ScriptValue>) return value;
    else if constexpr (std::is_same_v<T,std::string>) {
        if (!value.is_string()) throw std::invalid_argument("Expected string");
        return value.get<std::string>();
    } else if constexpr (std::is_same_v<T,bool>) {
        if (!value.is_boolean()) throw std::invalid_argument("Expected boolean");
        return value.get<bool>();
    } else if constexpr (std::is_same_v<T,int>) {
        if (!value.is_number_integer()) throw std::invalid_argument("Expected integer");
        if (value.is_number_unsigned()) {
            auto number=value.get<std::uint64_t>();
            if(number>static_cast<std::uint64_t>(std::numeric_limits<int>::max()))throw std::out_of_range("Integer out of range");
            return static_cast<int>(number);
        }
        auto number=value.get<std::int64_t>();
        if(number<std::numeric_limits<int>::min()||number>std::numeric_limits<int>::max())throw std::out_of_range("Integer out of range");
        return static_cast<int>(number);
    } else if constexpr (std::is_same_v<T,double>) {
        if(!value.is_number())throw std::invalid_argument("Expected number");
        return value.get<double>();
    } else static_assert(!sizeof(T),"Unsupported native argument");
}
template<class R,class... Args,std::size_t... I>
ScriptValue call(R(*function)(Args...),const ScriptValue& args,std::index_sequence<I...>) {
    if(!args.is_array()||args.size()!=sizeof...(Args))throw std::invalid_argument("Wrong argument count");
    std::tuple<std::remove_cvref_t<Args>...> values{argument<std::remove_cvref_t<Args>>(args[I])...};
    if constexpr(std::is_void_v<R>){std::apply(function,values);return nullptr;}
    else return std::apply(function,values);
}
}
// Configure on a control thread, then start(). Runtime/Promise work stays on loop.
// Async completions run on that loop. Call shutdown and wait before stopping it.
class F_API FScriptEngine {
public:
    using Completion=std::function<void(FScriptResult)>;
    using NativeFunction=std::function<ScriptValue(const ScriptValue&)>;
    // Called promptly on the script loop. The host starts bounded asynchronous
    // work, completes exactly once (also after cancellation), and returns its handle.
    using NativeAsyncFunction=std::function<Async::OperationHandle(FScriptNativeCall,Completion)>;
    explicit FScriptEngine(FEventLoop&,FScriptOptions options={});
    ~FScriptEngine();
    FScriptEngine(const FScriptEngine&)=delete;
    FScriptEngine& operator=(const FScriptEngine&)=delete;
    template<class R,class... Args> void bind(std::string name,R(*function)(Args...)) {
        if(!function)throw std::invalid_argument("Native function must not be null");
        bindFunction(std::move(name),[function](const ScriptValue& args){return ScriptDetail::call(function,args,std::index_sequence_for<Args...>{});});
    }
    void bindFunction(std::string name,NativeFunction);
    void bindAsyncFunction(std::string name,NativeAsyncFunction);
    void addScript(std::string name,std::string file,std::chrono::milliseconds interval={});
    void start();
    Async::OperationHandle load(std::string name,FScriptSource,Completion,
        std::chrono::milliseconds interval={},std::function<void()> beforePublish={});
    Async::OperationHandle loadFile(std::string name,std::string file,Completion,
        std::chrono::milliseconds interval={},std::function<void()> beforePublish={});
    Async::OperationHandle invoke(std::string script,std::string hook,ScriptValue input,Completion,
        std::chrono::milliseconds timeout=std::chrono::seconds(30));
    void shutdown(std::function<void()> drained={});
    bool isInLoopThread() const;
    static FScriptSource readSource(const std::string& file);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
#define FEI_SCRIPT_BIND(engine,name,function) (engine).bind((name),&(function))
