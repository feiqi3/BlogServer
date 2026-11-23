#include "FeiHttpBinding.h"
#include "FEventLoop.h"
#include "Http/FHttpClient.h"
#include <stdexcept>
using namespace std::chrono_literals;

namespace Fei::Script {
void FeiHttpBinding::bindAsyncFunction(std::string name,std::string script,FScriptEngine::NativeAsyncFunction function){
    const auto index=asynchronous_.size();
    asynchronous_.push_back({std::move(name),std::move(script),std::move(function)});
    installNative([&](JSContext*,JSValueConst host){installAsync(host,index,0);});
}
void FeiHttpBinding::installAsync(JSValueConst host,std::size_t index,std::uint64_t owner){
    auto* ctx=runtime_.context();
    JSValue parent=JS_DupValue(ctx,host);
    try{
        const auto& name=asynchronous_.at(index).name;
        std::size_t begin=0;
        while(true){
            auto end=name.find('.',begin);auto part=name.substr(begin,end==std::string::npos?end:end-begin);
            if(end==std::string::npos){
                JSValue data[2]={JS_NewInt64(ctx,index),JS_NewInt64(ctx,owner)};
                auto function=JS_NewCFunctionData(ctx,nativeAsyncCall,0,0,2,data);
                JS_FreeValue(ctx,data[0]);JS_FreeValue(ctx,data[1]);
                if(JS_IsException(function)||JS_SetPropertyStr(ctx,parent,part.c_str(),function)<0)throw std::runtime_error("Cannot install asynchronous binding");
                break;
            }
            auto child=JS_GetPropertyStr(ctx,parent,part.c_str());
            if(JS_IsException(child))throw std::runtime_error("Cannot read binding namespace");
            if(JS_IsUndefined(child)){
                JS_FreeValue(ctx,child);child=JS_NewObjectProto(ctx,JS_NULL);
                if(JS_IsException(child))throw std::runtime_error("Cannot allocate binding namespace");
                if(JS_SetPropertyStr(ctx,parent,part.c_str(),JS_DupValue(ctx,child))<0){JS_FreeValue(ctx,child);throw std::runtime_error("Cannot install binding namespace");}
            }else if(!JS_IsObject(child)||JS_IsFunction(ctx,child)){
                JS_FreeValue(ctx,child);throw std::runtime_error("Conflicting binding namespace");
            }
            JS_FreeValue(ctx,parent);parent=child;begin=end+1;
        }
    }catch(...){JS_FreeValue(ctx,parent);throw;}
    JS_FreeValue(ctx,parent);
}
JSValue FeiHttpBinding::nativeAsyncCall(JSContext* ctx,JSValueConst,int argc,JSValueConst* argv,int,JSValue* data){
    auto* self=static_cast<FeiHttpBinding*>(JS_GetContextOpaque(ctx));
    int64_t index=0,owner=0;
    if(JS_ToInt64(ctx,&index,data[0])<0||JS_ToInt64(ctx,&owner,data[1])<0)return JS_EXCEPTION;
    if(self->validating_||self->closing_||!self->failure_.empty())return JS_ThrowInternalError(ctx,"Native operations unavailable in this phase");
    if(owner&&!self->hooks_.contains(owner))return JS_ThrowInternalError(ctx,"Invocation has ended");
    if(self->nativePending_.size()>=64)return JS_ThrowInternalError(ctx,"Too many native operations");
    auto array=JS_NewArray(ctx);
    if(JS_IsException(array))return array;
    for(int i=0;i<argc;++i)if(JS_SetPropertyUint32(ctx,array,i,JS_DupValue(ctx,argv[i]))<0){JS_FreeValue(ctx,array);return JS_EXCEPTION;}
    auto json=JS_JSONStringify(ctx,array,JS_UNDEFINED,JS_UNDEFINED);JS_FreeValue(ctx,array);
    if(JS_IsException(json))return json;
    size_t length=0;const char* text=JS_ToCStringLen(ctx,&length,json);
    if(!text){JS_FreeValue(ctx,json);return JS_EXCEPTION;}
    FScriptNativeCall call;
    try{
        if(length>self->maxJsonBytes_)throw std::invalid_argument("Native arguments exceed JSON budget");
        call.arguments=ScriptValue::parse(text,text+length);
        const auto& registration=self->asynchronous_.at(index);
        call.script=registration.script;call.hook=self->invocationName(owner);call.invocationId=owner;
    }catch(const std::exception& error){JS_FreeCString(ctx,text);JS_FreeValue(ctx,json);return JS_ThrowTypeError(ctx,"%s",error.what());}
    JS_FreeCString(ctx,text);JS_FreeValue(ctx,json);
    JSValue functions[2];auto promise=JS_NewPromiseCapability(ctx,functions);
    if(JS_IsException(promise))return promise;
    auto pending=std::make_shared<NativePending>();pending->id=++self->nextId_;pending->owner=owner;
    pending->resolve=functions[0];pending->reject=functions[1];self->nativePending_.emplace(pending->id,pending);
    auto keep=self->shared_from_this();
    auto complete=[keep,pending](FScriptResult result){
        if(pending->delivered.exchange(true))return;
        keep->client_.loop().AddTask([keep,pending,result=std::move(result)]()mutable{keep->completeNative(pending,std::move(result));});
    };
    try{pending->handle=self->asynchronous_.at(index).function(std::move(call),complete);}
    catch(const std::exception& error){complete({nullptr,{Async::ErrorCode::UserException,error.what()}});}
    catch(...){complete({nullptr,{Async::ErrorCode::UserException,"Native operation failed"}});}
    return promise;
}
void FeiHttpBinding::completeNative(std::shared_ptr<NativePending> pending,FScriptResult result){
    checkLoop();auto* ctx=runtime_.context();
    if(!closing_&&failure_.empty()&&(!pending->owner||hooks_.contains(pending->owner))){
        std::string json;
        try{if(result.ok()){json=result.value.dump();if(json.size()>maxJsonBytes_)throw std::length_error("Native result exceeds JSON budget");}}
        catch(const std::exception& error){result.error={Async::ErrorCode::LimitExceeded,error.what()};}
        try{
            runtime_.resume(100ms,[&]{
                JSValue value=result.ok()?JS_ParseJSON(ctx,json.c_str(),json.size(),"native-result.json"):JS_NewString(ctx,result.error.message.c_str());
                if(JS_IsException(value))throw std::runtime_error("Cannot allocate native result");
                auto returned=JS_Call(ctx,result.ok()?pending->resolve:pending->reject,JS_UNDEFINED,1,&value);JS_FreeValue(ctx,value);
                if(JS_IsException(returned))throw std::runtime_error(runtime_.consumeException());
                JS_FreeValue(ctx,returned);
            });
            runtime_.checkUnhandled();
        }catch(const std::exception& error){fault(error.what());}
    }
    JS_FreeValue(ctx,pending->resolve);JS_FreeValue(ctx,pending->reject);
    nativePending_.erase(pending->id);notifyClosed();
}
}
