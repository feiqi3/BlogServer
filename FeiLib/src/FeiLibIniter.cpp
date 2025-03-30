#include "FeiLibIniter.h"
#include "FSocket.h"
#include "FLogger.h"
#include "FConfigReader.h"
#include <algorithm>

namespace Fei {
namespace {
    
    lvl getLvl(const std::string& level){
        if (level == "trace") {
            return lvl::trace;
        }
        if (level == "debug") {
            return lvl::debug;
        }
        if (level == "info") {
            return lvl::info;
        }
        if (level == "warn") {
            return lvl::warn;
        }
        if (level == "err") {
            return lvl::err;
        }
        if (level == "critical") {
            return lvl::critical;
        }
        if (level == "off") {
            return lvl::off;
        }
        return lvl::n_levels;
    }

    void setLoggerConfigByConfig(LoggerConfig& cfg) {
        auto configger = FConfigReader::instance();
        auto showlog = configger->getCfg("LogShowLevel");
        if (showlog) {
            auto l = getLvl(showlog.value());
            l = l == lvl::n_levels ? lvl::info : l;
            cfg.showLvl = l;
        }

        auto savelog = configger->getCfg("LogSaveLevel");
        if (savelog) {
            auto l = getLvl(savelog.value());
            l = l == lvl::n_levels ? lvl::info : l;
            cfg.saveLvl = l;
        }

        auto savename = configger->getCfg("LogFile");
        if (savename) {
            cfg.saveFileName = savename.value();
        }

        auto LogFlushTime = configger->getCfg("LogFlushTime");
        if (LogFlushTime) {
            cfg.flushTime =(unsigned int) std::min(std::stof(LogFlushTime.value()),1000.f);
        }
    }
}



F_API void FeiLibInit(const std::string& cfgDir) {
    FeiInit();
    new FConfigReader(cfgDir);
    LoggerConfig config{.showLvl = lvl::trace};
    setLoggerConfigByConfig(config);
    Logger* log = new Logger(config);
    log->log(lvl::info,"FeiLib Init.");
    (void)cfgDir;
    (void)log;
}

F_API void FeiLibUnInit() {
    FeiUnInit();
    Logger::instance()->log(lvl::info,"FeiLib Uninit.");
    delete Logger::instance();
    delete FConfigReader::instance();
}

}; // namespace Fei
