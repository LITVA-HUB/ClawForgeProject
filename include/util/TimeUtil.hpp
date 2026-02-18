#pragma once

#include <chrono>
#include <string>

namespace clawforge::util {

class TimeUtil {
 public:
  static int64_t nowMillis();
  static std::string nowIso8601();
  static int64_t parseIso8601Utc(const std::string& iso8601);
};

}  // namespace clawforge::util
