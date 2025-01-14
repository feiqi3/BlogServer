#include "FLogger.h"
#include "Flogger.h"
#include "spdlog/async_logger.h"
#include "spdlog/common.h"
#include "spdlog/logger.h"
#include "spdlog/spdlog.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>

namespace Fei {
class LoggerPrivate {
public:
  std::shared_ptr<spdlog::logger> mConsoleLogger = 0;
  std::shared_ptr<spdlog::logger> mFileLogger = 0;
};

namespace {
spdlog::level::level_enum toSpdLvl(lvl _lvl) {
  switch (_lvl) {

  case lvl::trace:
    return spdlog::level::trace;
  case lvl::debug:
    return spdlog::level::debug;
  case lvl::info:
    return spdlog::level::info;
  case lvl::warn:
    return spdlog::level::warn;
  case lvl::err:
    return spdlog::level::err;
  case lvl::critical:
    return spdlog::level::critical;
  case lvl::off:
    return spdlog::level::off;
  case lvl::n_levels:
  default:
    return spdlog::level::n_levels;
    break;
  }
}
} // namespace

Logger::Logger(const LoggerConfig &config)
    : mShowLvl((lvl)std::min((int)config.showLvl, (int)config.saveLvl)),
      mDp(new LoggerPrivate) {
  mDp->mConsoleLogger = spdlog::stdout_logger_mt("Console_log");
  mDp->mConsoleLogger->set_level(toSpdLvl(config.showLvl));
  mDp->mConsoleLogger->set_pattern("[%l] %v");
  mDp->mFileLogger = spdlog::basic_logger_mt("File_log", config.saveFileName);
  mDp->mFileLogger->set_level(toSpdLvl(config.saveLvl));
  mDp->mFileLogger->set_pattern("[%Y-%m-%d %T] [%l] %v");
}

Logger::~Logger() {
  mDp->mFileLogger->flush();
  delete mDp;
}

void Logger::_logInner(const std::string &str, lvl lvl) {
  switch (lvl) {
  case lvl::trace: {
    mDp->mConsoleLogger->trace(str);
    mDp->mFileLogger->trace(str);
  } break;
  case lvl::debug: {
    mDp->mConsoleLogger->debug(str);
    mDp->mFileLogger->debug(str);
  } break;
  case lvl::info: {
    mDp->mConsoleLogger->info(str);
    mDp->mFileLogger->info(str);
  } break;
  case lvl::warn: {
    mDp->mConsoleLogger->warn(str);
    mDp->mFileLogger->warn(str);
  } break;
  case lvl::critical: {
    mDp->mConsoleLogger->critical(str);
    mDp->mFileLogger->critical(str);
    mDp->mFileLogger->flush();
    std::abort();
    break;
  }
  case lvl::err: {
    mDp->mConsoleLogger->error(str);
    mDp->mFileLogger->error(str);
    mDp->mFileLogger->flush();
  } break;
  case lvl::off:
  case lvl::n_levels:
  default:
    break;
  }
}

void Logger::_logInner(const char *moduleName, const std::string &str,
                       lvl lvl) {
  _logInner(fmt::format("[{}]{}", moduleName, str), lvl);
}
} // namespace Fei
