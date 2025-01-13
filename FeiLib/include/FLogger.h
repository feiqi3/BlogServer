#ifndef FLOGGER_H_
#define FLOGGER_H_
#pragma once

#include "FSingleton.h"
#include "spdlog/fmt/fmt.h"

namespace Fei {

// lvl::critical  --> abort this program
enum class lvl {
  trace = 0,
  debug = 1,
  info = 2,
  warn = 3,
  err = 4,
  critical = 5,
  off = 6,
  n_levels = 7
};

struct LoggerConfig {
  std::string saveFileName = "log.txt";
  unsigned int flushTime = 3000; // ms
  lvl showLvl = lvl::info;
  lvl saveLvl = lvl::info;
};

class F_API Logger : public FSingleton<Logger> {
public:
  Logger(const LoggerConfig &config);
  template <typename... Args>
  void log(lvl lvl, const char *fmt, Args &&...args) {
    if (!shouldShow(lvl)) {
      return;
    }
    _logInner(fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...), lvl);
  }

  template <typename... Args>
  void log(const char *moduleName, lvl lvl, const char *fmt, Args &&...args) {
    if (!shouldShow(lvl)) {
      return;
    }
    _logInner(moduleName,
              fmt::format(fmt::runtime(fmt), std::forward<Args>(args)...), lvl);
  }

  ~Logger();

private:
  lvl mShowLvl;
  class LoggerPrivate *mDp = 0;

private:
  bool shouldShow(lvl _lvl) const { return (int)_lvl >= (int)mShowLvl; }
  void _logInner(const std::string &str, lvl lvl);
  void _logInner(const char *moduleName, const std::string &str, lvl lvl);
};
} // namespace Fei
#endif // !LOGGER_H_
