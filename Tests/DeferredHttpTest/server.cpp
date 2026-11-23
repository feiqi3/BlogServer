#include "Http/FHttpServer.h"
#include "Http/FController.h"
#include "FeiLibIniter.h"
#include "FEventLoop.h"
#include "FBuffer.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <stdexcept>
using namespace Fei::Http;
using namespace std::chrono_literals;
namespace {
Fei::FEventLoop* jobs=nullptr;
std::atomic_bool closing=false;
std::atomic_int cancelled=0,rejected=0,started=0,sent=0;
std::map<std::string,FHttpResponder> gates;
FHttpResponse text(std::string body){FHttpResponse response;response.setBody(std::move(body));return response;}
class Controller : public FControllerBase {
public:
    Controller():FControllerBase("deferred-test"){}
    FHttpResponse get(const FHttpRequest&,const FPathVar& vars){
        auto kind=vars.get("kind"),value=vars.get("value");
        if(kind=="fast")return text(value);
        if(kind=="unavailable"){auto response=text("unavailable");response.setStatusCode(StatusCode::_503);return response;}
        if(kind=="stats")return text(std::to_string(started)+","+std::to_string(cancelled)+","+std::to_string(rejected)+","+std::to_string(sent));
        if(kind=="release"){
            auto pending=std::move(gates);gates.clear();
            for(auto& [name,responder]:pending)responder.complete(text(name));
            return text("released");
        }
        if(kind=="shutdown"){closing=true;return text("stopping");}
        if(kind=="throw")return FHttpResponse::defer([](auto){throw std::runtime_error("test");});
        return FHttpResponse::defer([kind,value](FHttpResponder responder){
            ++started;
            responder.onCancel([]{++cancelled;});
            if(kind=="gate"){gates[value]=responder;return;}
            jobs->RunAfter(kind=="timeout"?200:80,[responder,value]{
                if(!responder.complete(text(value)))++rejected;
                if(!responder.complete(text("duplicate")))++rejected;
            });
        },kind=="timeout"?25ms:5s);
    }
    REGISTER_MAPPING_BEGIN("")
        REGISTER_MAPPING_FUNC(Method::GET,"/{kind}/{value}",Controller,get);
    REGISTER_MAPPING_END
};
REGISTER_CONTROLLER_CLASS(Controller)
}
int main(int argc,char** argv){
    if(argc!=4)return 2;
    if(!text("body").toString().ends_with("\r\n\r\nbody"))return 5;
    for(const auto& first:{std::string("GET /one HTTP/1.1\r\nHost: test\r\n\r\n"),std::string("POST /one HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc")}){
        std::string next="GET /two HTTP/1.1\r\nHost: test\r\n\r\n";
        Fei::FBuffer buffer(256);auto bytes=first+next;buffer.Append(bytes.data(),bytes.size());
        Fei::FBufferReader reader(buffer);FHttpParser parser;
        if(!parser.parse(reader)||reader.getBufferReadableSize()!=static_cast<int>(next.size())||reader.readNext()!='G')return 4;
    }
    Fei::FeiLibInit(std::string(argv[1])+"/server.cfg");
    FHttpServer::initSSLenv(std::string(argv[1])+"/cert.pem",std::string(argv[1])+"/private.pem");
    {
        FHttpServer server(1);jobs=&server.jobLoop();
        server.setPreSendResponseCB([](const auto&,auto& response){
            if(response.isDeferred())std::terminate();
            response.addHeader("X-Finalized","once");++sent;
        });
        server.addListenPort(std::stoi(argv[2]));server.addSSLPort(std::stoi(argv[3]));server.run();
        while(!closing)std::this_thread::sleep_for(5ms);
        server.stop();
        for(auto& [name,responder]:gates)if(!responder.cancelled()||responder.complete(text("late")))return 3;
        gates.clear();
    }
    FHttpServer::deinitSSLenv();Fei::FeiLibUnInit();
}
