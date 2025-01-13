#ifndef FLOGGER_H_
#define FLOGGER_H_
#pragma once

#include "FSingleton.h"
#include "spdlog/fmt/fmt.h"

namespace Fei {

//lvl::critical  --> abort this program
enum class lvl { trace, debug, info, warn, err, critical, off, n_levels };

struct LoggerConfig {
  std::string saveFileName = "log.txt";
  unsigned int flushTime = 3000; // ms
  lvl showLvl = lvl::info;
  lvl saveLvl = lvl::info;
};

class Logger : public FSingleton<Logger> {
public:
  Logger(const LoggerConfig &config);

  template <typename... Args>
  void log(lvl lvl, const char *fmt, Args &&...args) {
    _logInner(fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...), lvl);
  }

  template <typename... Args>
  void log(const char *moduleName, lvl lvl, const char *fmt, Args &&...args) {
    _logInner(moduleName,
              fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...), lvl);
  }

  ~Logger();

private:
  void _logInner(const std::string &str, lvl lvl);
  void _logInner(const char *moduleName, const std::string &str, lvl lvl);
  class LoggerPrivate *mDp = 0;
};
} // namespace Fei
#endif // !LOGGER_H_
