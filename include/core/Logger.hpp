#pragma once

#include <mutex>
#include <string>

namespace clawforge::core {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

class Logger {
 public:
  static void setLevel(LogLevel level);
  static void debug(const std::string& msg);
  static void info(const std::string& msg);
  static void warn(const std::string& msg);
  static void error(const std::string& msg);

 private:
  static void log(LogLevel level, const std::string& msg);
  static const char* levelName(LogLevel level);

  static std::mutex mutex_;
  static LogLevel level_;
};

}  // namespace clawforge::core
