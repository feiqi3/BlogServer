
#include "Server/server.h"
#include "FLogger.h"
#include "Shutdown.h"

extern Blog::Server* g_Server;

namespace Blog{
    void shutdownServer(){
        g_Server->shutdown();
        Fei::Logger::instance()->log(Fei::lvl::info, "Try Shutdown Server.");
    }
}

