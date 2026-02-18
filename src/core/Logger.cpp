#include "core/Logger.hpp"

#include <iostream>

#include "util/TimeUtil.hpp"

namespace clawforge::core {

std::mutex Logger::mutex_;
LogLevel Logger::level_ = LogLevel::Info;

void Logger::setLevel(LogLevel level) { level_ = level; }

void Logger::debug(const std::string& msg) { log(LogLevel::Debug, msg); }
void Logger::info(const std::string& msg) { log(LogLevel::Info, msg); }
void Logger::warn(const std::string& msg) { log(LogLevel::Warn, msg); }
void Logger::error(const std::string& msg) { log(LogLevel::Error, msg); }

void Logger::log(LogLevel level, const std::string& msg) {
  if (static_cast<int>(level) < static_cast<int>(level_)) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  std::cout << "[" << util::TimeUtil::nowIso8601() << "] [" << levelName(level) << "] " << msg
            << std::endl;
}

const char* Logger::levelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
    default:
      return "LOG";
  }
}

}  // namespace clawforge::core
