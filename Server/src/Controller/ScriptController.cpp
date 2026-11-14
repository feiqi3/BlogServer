#include "ScriptController.h"
#include "Service/AdminLogin.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <optional>
#ifdef FEILIB_ENABLE_SCRIPT
#include "Service/ScriptAdminService.h"
#endif
namespace Blog {
namespace {
using namespace Fei::Http;
FHttpResponse reply(StatusCode status, int code, nlohmann::json body) {
    FHttpResponse response;
    body["result"] = code;
    response.setStatusCode(status).setContentType("application/json; charset=utf-8")
        .addHeader("Cache-Control", "no-store").addHeader("X-Content-Type-Options", "nosniff").setBody(body.dump());
    return response;
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c){return std::tolower(c);});
    return value;
}
std::string header(const FHttpRequest& request, const std::string& name) {
    std::string value;
    request.traverseHeaders([&](const auto& entry){
        if (lower(entry.first) == name) {value = entry.second; return false;} return true;
    });
    return value;
}
std::optional<FHttpResponse> authorize(const FHttpRequest& request, bool mutate) {
    if (!AdminLogin::instance()->isLogin(request)) return reply(StatusCode::_401,401,{{"error","Login required"}});
    if (!mutate) return {};
    // Custom header prevents cross-origin simple form submissions, including debug cookies.
    auto type = lower(header(request,"content-type"));
    auto origin = lower(header(request,"origin"));
    auto host = lower(header(request,"host"));
    if (host.empty()) host=lower(header(request,":authority"));
    bool sameOrigin=origin.empty() || (!host.empty() && (origin=="http://"+host || origin=="https://"+host));
    if (header(request,"x-blog-script-request")!="1" || type.substr(0,type.find(';'))!="application/json" ||
        !sameOrigin || lower(header(request,"sec-fetch-site"))=="cross-site")
        return reply(StatusCode::_403,403,{{"error","Same-origin JSON request required"}});
    return {};
}
FHttpResponse unavailable() {return reply(StatusCode::_501,501,{{"enabled",false},{"error","Script administration is not enabled"}});}
#ifdef FEILIB_ENABLE_SCRIPT
FHttpResponse submitted(const Script::ScriptAdminService::Submission& submission) {
    if (!submission.error.empty()) return reply(StatusCode::_429,429,{{"error",submission.error}});
    return reply(StatusCode::_202,202,{{"jobId",submission.id}});
}
#endif
}
Fei::Http::FHttpResponse ScriptController::Status(const Fei::Http::FHttpRequest& request,const Fei::Http::FPathVar&) {
    if(auto denied=authorize(request,false))return *denied;
#ifdef FEILIB_ENABLE_SCRIPT
    if(auto service=Script::ScriptAdminService::current())return reply(Fei::Http::StatusCode::_200,200,service->status());
#endif
    return unavailable();
}
Fei::Http::FHttpResponse ScriptController::Job(const Fei::Http::FHttpRequest& request,const Fei::Http::FPathVar& path) {
    if(auto denied=authorize(request,false))return *denied;
#ifdef FEILIB_ENABLE_SCRIPT
    if(auto service=Script::ScriptAdminService::current()) {
        auto job=service->job(path.get("id"));
        if(job.is_null())return reply(Fei::Http::StatusCode::_404,404,{{"error","Job expired or unknown"}});
        return reply(Fei::Http::StatusCode::_200,200,std::move(job));
    }
#endif
    return unavailable();
}
Fei::Http::FHttpResponse ScriptController::Upload(const Fei::Http::FHttpRequest& request,const Fei::Http::FPathVar&) {
    if(auto denied=authorize(request,true))return *denied;
#ifdef FEILIB_ENABLE_SCRIPT
    auto service=Script::ScriptAdminService::current();
    if(!service)return unavailable();
    try {
        if(request.getRequestBody().size()>4*1024*1024)throw std::invalid_argument("Upload exceeds 4 MiB JSON limit");
        auto body=nlohmann::json::parse(request.getRequestBody());
        if(!body.is_object() || !body.contains("entry") || !body["entry"].is_string() ||
            !body.contains("files") || !body["files"].is_object() || body["files"].empty() || body["files"].size()>32)
            throw std::invalid_argument("Expected entry and 1-32 JavaScript files");
        Fei::FScriptSource source;source.entry=body["entry"].get<std::string>();
        std::size_t bytes=0;
        for(const auto& [name,value]:body["files"].items()) {
            if(name.size()>160 || !value.is_string())throw std::invalid_argument("Invalid module file");
            auto text=value.get<std::string>();bytes+=text.size();
            if(text.size()>128*1024 || bytes>512*1024)throw std::invalid_argument("Script package is too large");
            source.sources.emplace(name,std::move(text));
        }
        return submitted(service->upload(std::move(source)));
    }catch(const std::exception&){return reply(Fei::Http::StatusCode::_400,400,{{"error","Invalid script package (32 files, 128 KiB per file, 512 KiB total)"}});}
#else
    return unavailable();
#endif
}
Fei::Http::FHttpResponse ScriptController::Rollback(const Fei::Http::FHttpRequest& request,const Fei::Http::FPathVar&) {
    if(auto denied=authorize(request,true))return *denied;
#ifdef FEILIB_ENABLE_SCRIPT
    if(auto service=Script::ScriptAdminService::current())return submitted(service->rollback());
#endif
    return unavailable();
}
}
