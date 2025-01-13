#ifndef FEILIBINITER_H
#define FEILIBINITER_H
#include "FeiLibIniter.h"
#include "FSocket.h"
#include "FLogger.h"
namespace Fei {

void FeiLibInit() {
    FeiInit();
    LoggerConfig config{};
    Logger* log = new Logger(config);
    log->log(lvl::info,"FeiLib Init.");
    (void)log;
}

void FeiLibUnInit() {
    FeiUnInit();
    Logger::instance()->log(lvl::info,"FeiLib Uninit.");
    delete Logger::instance();
}

}; // namespace Fei

#endif