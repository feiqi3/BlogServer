#include "FeiHttpBinding.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace Fei::Script {
namespace {
struct Value {
    JSContext* ctx;JSValue value;
    Value(JSContext* ctx,JSValue value):ctx(ctx),value(value){}
    ~Value(){JS_FreeValue(ctx,value);}
    Value(const Value&)=delete;
    JSValue release(){return std::exchange(value,JS_UNDEFINED);}
};
struct Text {
    JSContext* ctx;std::size_t size=0;const char* value;
    Text(JSContext* ctx,JSValueConst input):ctx(ctx),value(JS_ToCStringLen(ctx,&size,input)){}
    ~Text(){if(value)JS_FreeCString(ctx,value);}
    std::string_view view()const{return {value,size};}
};
struct Properties {
    JSContext* ctx;JSPropertyEnum* values=nullptr;std::uint32_t size=0;bool valid;
    Properties(JSContext* ctx,JSValueConst object):ctx(ctx),valid(JS_GetOwnPropertyNames(ctx,&values,&size,object,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)>=0){}
    ~Properties(){for(std::uint32_t i=0;i<size;++i)JS_FreeAtom(ctx,values[i].atom);js_free(ctx,values);}
};
bool type(JSContext* ctx,const char* message){JS_ThrowTypeError(ctx,"%s",message);return false;}
bool range(JSContext* ctx,const char* message){JS_ThrowRangeError(ctx,"%s",message);return false;}
bool number(JSContext* ctx,JSValueConst object,const char* key,std::uint64_t maximum,std::uint64_t& output){
    Value input(ctx,JS_GetPropertyStr(ctx,object,key));if(JS_IsException(input.value))return false;
    if(JS_IsUndefined(input.value))return true;
    if(!JS_IsNumber(input.value))return type(ctx,"HTTP limits must be numbers");
    double value;if(JS_ToFloat64(ctx,&value,input.value)<0)return false;
    if(!std::isfinite(value)||std::trunc(value)!=value||value<1||value>maximum)return range(ctx,"HTTP limit is out of range");
    output=static_cast<std::uint64_t>(value);return true;
}
bool token(std::string_view value){
    if(value.empty())return false;
    for(unsigned char c:value)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view("!#$%&'*+-.^_`|~").find(c)!=std::string_view::npos))return false;
    return true;
}
bool set(JSContext* ctx,JSValueConst object,const char* name,JSValue value){
    return !JS_IsException(value)&&JS_DefinePropertyValueStr(ctx,object,name,value,JS_PROP_C_W_E)>=0;
}
bool item(JSContext* ctx,JSValueConst object,std::uint32_t index,JSValue value){
    return !JS_IsException(value)&&JS_DefinePropertyValueUint32(ctx,object,index,value,JS_PROP_C_W_E)>=0;
}
}

bool FeiHttpBinding::httpOptions(JSContext* ctx,JSValueConst options,Fei::Http::ClientRequest& request,bool& binary){
    if(JS_IsUndefined(options))return true;
    if(!JS_IsObject(options)||JS_IsFunction(ctx,options)||JS_IsArray(ctx,options))return type(ctx,"HTTP options must be an object");
    Properties keys(ctx,options);if(!keys.valid)return false;
    if(keys.size>4)return type(ctx,"Unknown HTTP option");
    for(std::uint32_t i=0;i<keys.size;++i){
        Value name(ctx,JS_AtomToString(ctx,keys.values[i].atom));if(JS_IsException(name.value))return false;
        Text key(ctx,name.value);if(!key.value)return false;
        if(key.view()!="responseType"&&key.view()!="headers"&&key.view()!="timeoutMs"&&key.view()!="maxBytes")return type(ctx,"Unknown HTTP option");
    }
    Value responseType(ctx,JS_GetPropertyStr(ctx,options,"responseType"));if(JS_IsException(responseType.value))return false;
    if(!JS_IsUndefined(responseType.value)){
        if(!JS_IsString(responseType.value))return type(ctx,"responseType must be text or arrayBuffer");
        Text value(ctx,responseType.value);if(!value.value)return false;
        if(value.view()=="arrayBuffer")binary=true;
        else if(value.view()!="text")return type(ctx,"responseType must be text or arrayBuffer");
    }
    std::uint64_t timeout=request.timeout.count(),bytes=request.maxBodyBytes;
    if(!number(ctx,options,"timeoutMs",30000,timeout)||!number(ctx,options,"maxBytes",4*1024*1024,bytes))return false;
    request.timeout=std::chrono::milliseconds(timeout);request.maxBodyBytes=static_cast<std::size_t>(bytes);
    Value headers(ctx,JS_GetPropertyStr(ctx,options,"headers"));if(JS_IsException(headers.value))return false;
    if(JS_IsUndefined(headers.value))return true;
    if(!JS_IsObject(headers.value)||JS_IsFunction(ctx,headers.value)||JS_IsArray(ctx,headers.value))return type(ctx,"HTTP headers must be an object");
    Properties fields(ctx,headers.value);if(!fields.valid)return false;
    if(fields.size>32)return range(ctx,"At most 32 request headers are allowed");
    std::size_t total=0;
    for(std::uint32_t i=0;i<fields.size;++i){
        Value name(ctx,JS_AtomToString(ctx,fields.values[i].atom));if(JS_IsException(name.value))return false;
        Text key(ctx,name.value);if(!key.value)return false;
        Value field(ctx,JS_GetProperty(ctx,headers.value,fields.values[i].atom));if(JS_IsException(field.value))return false;
        if(!JS_IsString(field.value))return type(ctx,"Header values must be strings");
        Text value(ctx,field.value);if(!value.value)return false;
        total+=key.size+value.size+4;if(total>8192)return range(ctx,"Request headers exceed 8192 bytes");
        if(!token(key.view())||std::any_of(value.view().begin(),value.view().end(),[](unsigned char c){return c==127||(c<32&&c!='\t');}))return type(ctx,"Invalid HTTP header");
        std::string lower(key.view());for(char& c:lower)if(c>='A'&&c<='Z')c+='a'-'A';
        if(lower=="host"||lower=="content-length"||lower=="transfer-encoding"||lower=="connection"||lower=="upgrade")return type(ctx,"Header is managed by the HTTP client");
        request.headers.emplace_back(key.view(),value.view());
    }
    return true;
}

JSValue FeiHttpBinding::httpResponse(JSContext* ctx,const Fei::Http::ClientResponse& response,bool binary){
    Value object(ctx,JS_NewObject(ctx));if(JS_IsException(object.value))return JS_EXCEPTION;
    if(!set(ctx,object.value,"status",JS_NewInt32(ctx,response.status)))return JS_EXCEPTION;
    JSValue body=binary?JS_NewArrayBufferCopy(ctx,reinterpret_cast<const std::uint8_t*>(response.body.data()),response.body.size())
        :JS_NewStringLen(ctx,response.body.data(),response.body.size());
    if(!set(ctx,object.value,"body",body))return JS_EXCEPTION;
    Value headers(ctx,JS_NewArray(ctx));if(JS_IsException(headers.value))return JS_EXCEPTION;
    for(std::uint32_t i=0;i<response.headers.size();++i){
        const auto& [key,value]=response.headers[i];Value pair(ctx,JS_NewArray(ctx));
        if(JS_IsException(pair.value)||!item(ctx,pair.value,0,JS_NewStringLen(ctx,key.data(),key.size()))||
            !item(ctx,pair.value,1,JS_NewStringLen(ctx,value.data(),value.size()))||!item(ctx,headers.value,i,pair.release()))return JS_EXCEPTION;
    }
    if(!set(ctx,object.value,"headers",headers.release()))return JS_EXCEPTION;
    return object.release();
}
}
