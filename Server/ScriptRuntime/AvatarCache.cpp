#ifdef FEILIB_ENABLE_SCRIPT
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "AvatarCache.h"
#include "Utils/ImageInfo.h"
#include <openssl/evp.h>
#include <chrono>
#include <fstream>
#include <algorithm>
#include <cctype>

namespace Blog::Script {
namespace {
namespace fs=std::filesystem;
using Json=Fei::ScriptValue;
using Response=Fei::Http::FHttpResponse;
using Status=Fei::Http::StatusCode;
constexpr std::size_t maxBytes=128*1024;
std::string hash(const std::string& bytes){
    unsigned char digest[32];unsigned length=0;
    if(EVP_Digest(bytes.data(),bytes.size(),digest,&length,EVP_sha256(),nullptr)!=1||length!=32)throw std::runtime_error("Cannot hash avatar");
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(auto c:digest){result+=hex[c>>4];result+=hex[c&15];}return result;
}
void source(const std::string& value){
    if(value.size()>2048||!(value.starts_with("https://")||value.starts_with("http://"))||
       std::any_of(value.begin(),value.end(),[](unsigned char c){return c<=32||c==127;}))throw std::invalid_argument("Expected an HTTP avatar URL of at most 2048 bytes");
}
std::string decode(const std::string& text){
    if(text.empty()||text.size()%4||text.size()>4*((maxBytes+2)/3))throw std::invalid_argument("Avatar exceeds 128 KiB");
    auto padding=text.ends_with("==")?2:text.ends_with("=")?1:0;
    for(std::size_t i=0;i<text.size();++i){
        auto c=text[i];if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'){
            if(i>=text.size()-padding)throw std::invalid_argument("Invalid avatar base64");
        }else if(c!='='||i<text.size()-padding)throw std::invalid_argument("Invalid avatar base64");
    }
    std::string result(text.size()/4*3,'\0');
    int size=EVP_DecodeBlock(reinterpret_cast<unsigned char*>(result.data()),reinterpret_cast<const unsigned char*>(text.data()),static_cast<int>(text.size()));
    if(size<0||std::size_t(size-padding)>maxBytes)throw std::invalid_argument("Invalid avatar base64 or size");
    result.resize(size-padding);return result;
}
std::string mime(const std::string& bytes){
    Utils::ImageInfo info;
    if(Utils::readImageInfo(bytes,info,16'777'216)){
        switch(info.format){
        case Utils::ImageFormat::Png:return "image/png";
        case Utils::ImageFormat::Jpeg:return "image/jpeg";
        case Utils::ImageFormat::Gif:return "image/gif";
        case Utils::ImageFormat::WebP:return "image/webp";
        default:break;
        }
    }
    auto text=std::string_view(bytes);
    if(text.starts_with("\xef\xbb\xbf"))text.remove_prefix(3);
    auto trim=[&]{while(!text.empty()&&std::isspace(static_cast<unsigned char>(text.front())))text.remove_prefix(1);};trim();
    if(text.starts_with("<?xml")){auto end=text.find("?>");if(end==text.npos)throw std::invalid_argument("Invalid SVG");text.remove_prefix(end+2);trim();}
    while(text.starts_with("<!--")){auto end=text.find("-->");if(end==text.npos)throw std::invalid_argument("Invalid SVG");text.remove_prefix(end+3);trim();}
    std::string lower(bytes);for(char& c:lower)if(c>='A'&&c<='Z')c+='a'-'A';
    if(text.starts_with("<svg")&&text.size()>4&&(text[4]=='>'||std::isspace(static_cast<unsigned char>(text[4])))&&
       lower.find("<!doctype")==lower.npos&&lower.find("<!entity")==lower.npos&&lower.find("<script")==lower.npos&&lower.find("<foreignobject")==lower.npos)
        return "image/svg+xml";
    throw std::invalid_argument("Avatar must be PNG, JPEG, GIF, WebP or SVG");
}
Json header(std::ifstream& input){
    std::string line;
    for(char c;input.get(c);){if(c=='\n')return Json::parse(line);if(line.size()>=512)break;line+=c;}
    throw std::runtime_error("Invalid avatar cache record");
}
Response missing(){Response response;return response.setStatusCode(Status::_404).addHeader("Cache-Control","no-store");}
void safeFile(const fs::path& file){if(fs::is_symlink(fs::symlink_status(file)))throw std::runtime_error("Invalid avatar cache file");}
}
AvatarCache::AvatarCache(Fei::FEventLoop& loop,const std::string& directory):loop_(loop){
    fs::create_directories(directory);root_=fs::canonical(directory);
}
std::shared_ptr<AvatarCache> AvatarCache::create(Fei::FEventLoop& loop,const std::string& directory){
    auto value=std::shared_ptr<AvatarCache>(new AvatarCache(loop,directory));std::shared_ptr<AvatarCache> empty;
    if(!current_.compare_exchange_strong(empty,value))throw std::logic_error("Avatar cache already configured");
    return value;
}
Json AvatarCache::lookup(const std::string& url){
    source(url);auto file=root_/(hash(url)+".avatar");safeFile(file);
    std::ifstream input(file,std::ios::binary);if(!input)return nullptr;
    return header(input);
}
Json AvatarCache::store(const std::string& url,const std::string& base64,Fei::Async::CancellationToken token){
    source(url);auto key=hash(url);auto file=root_/(key+".avatar");safeFile(file);
    if(!fs::exists(file)){
        std::size_t count=0;for(const auto& entry:fs::directory_iterator(root_))if(entry.path().extension()==".avatar")++count;
        if(count>=256)throw std::runtime_error("Avatar cache is full (256 URLs)");
    }
    auto bytes=decode(base64);auto type=mime(bytes);
    Json record={{"cachedAt",std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
        {"type",type},{"etag",hash(bytes)},{"size",bytes.size()}};
    auto temporary=root_/(key+".tmp");
    try{
        safeFile(temporary);if(token.cancelled())throw std::runtime_error("Avatar store cancelled");
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output<<record.dump()<<'\n';output.write(bytes.data(),bytes.size());output.close();
        if(!output)throw std::runtime_error("Cannot persist avatar");
        if(token.cancelled())throw std::runtime_error("Avatar store cancelled");
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace avatar");
#else
        fs::rename(temporary,file);
#endif
    }catch(...){std::error_code ignored;fs::remove(temporary,ignored);throw;}
    return record;
}
void AvatarCache::bind(Fei::FScriptEngine& engine){
    auto self=shared_from_this();
    for(std::string operation:{"get","store"})engine.bindAsyncFunction("avatarCache."+operation,[self,operation](auto call,auto callback){
        return self->files_.submit([self,operation,args=std::move(call.arguments)](auto token)->std::any{
            if(token.cancelled())throw std::runtime_error("Avatar operation cancelled");
            if(!args.is_array()||args.size()!=(operation=="get"?1:2))throw std::invalid_argument("Invalid avatar cache arguments");
            auto url=args[0].template get<std::string>();
            return operation=="get"?self->lookup(url):self->store(url,args[1].template get<std::string>(),token);
        },[callback=std::move(callback)](auto result){
            if(result.ok())callback({std::any_cast<Json>(std::move(*result.value)),{}});else callback({nullptr,result.error});
        },&self->loop_);
    });
}
Response AvatarCache::image(std::string url,std::string etag){
    try{source(url);}catch(...){return missing();}
    auto self=shared_from_this();
    return Response::defer([self,url=std::move(url),etag=std::move(etag)](auto reply){
        auto handle=self->files_.submit([self,url,etag](auto token)->std::any{
            try{
                auto file=self->root_/(hash(url)+".avatar");safeFile(file);std::ifstream input(file,std::ios::binary);
                if(!input||token.cancelled())return missing();
                auto record=header(input);
                auto size=record.at("size").get<std::size_t>();if(size>maxBytes)return missing();
                const auto tag="\""+record.at("etag").get<std::string>()+"\"";Response response;
                response.setContentType(record.at("type").get<std::string>()).addHeader("ETag",tag)
                    .addHeader("Cache-Control","public, max-age=600").addHeader("X-Content-Type-Options","nosniff")
                    .addHeader("Content-Security-Policy","sandbox; default-src 'none'; style-src 'unsafe-inline'");
                if(etag==tag||etag=="W/"+tag||etag=="*")return response.setStatusCode(Status::_304);
                std::string bytes(size,'\0');input.read(bytes.data(),bytes.size());if(!input||input.peek()!=EOF)return missing();
                return response.setBody(std::move(bytes));
            }catch(...){return missing();}
        },[reply](auto result){if(result.ok())reply.complete(std::any_cast<Response>(std::move(*result.value)));else reply.complete(missing());},&self->loop_);
        reply.onCancel([handle]{handle.cancel();});
    });
}
void AvatarCache::shutdown(){
    if(closed_.exchange(true))return;
    auto expected=shared_from_this();current_.compare_exchange_strong(expected,{});files_.shutdown();
}
}
#endif
