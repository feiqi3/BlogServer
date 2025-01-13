#ifndef FEILIBINITER_H
#define FEILIBINITER_H
#include "FSocket.h"
#include "FeiLibIniter.h"
#include "FLogger.h"
namespace Fei {
F_API void FeiLibInit() {
    FeiInit();
    LoggerConfig config{.showLvl = lvl::trace};
    Logger* log = new Logger(config);
    log->log(lvl::info,"FeiLib Init.");
    (void)log;
}

F_API void FeiLibUnInit() {
    FeiUnInit();
    Logger::instance()->log(lvl::info,"FeiLib Uninit.");
    delete Logger::instance();
}

}; // namespace Fei

#endif