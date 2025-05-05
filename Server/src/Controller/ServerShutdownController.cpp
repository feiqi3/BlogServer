#include "ServerShutdownController.h"
#include "Service/QuickRedirect.h"
#include "Service/Shutdown.h"
namespace Blog {
    Fei::Http::FHttpResponse ServerShutdownController::Shutdown(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar&){
        auto addr = req.getAddrIn();
        char maxBuf[128];
        addr.toHumanFriendyType(maxBuf, 128, nullptr);
        std::string ip = maxBuf;
        if(ip == "127.0.0.1"){
            shutdownServer();
        }
        return Redirector::RedirectTo("/404");
    }
}
