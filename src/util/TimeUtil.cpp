#include "util/TimeUtil.hpp"

#include <ctime>

namespace clawforge::util {

int64_t TimeUtil::nowMillis() {
  const auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::string TimeUtil::nowIso8601() {
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);

  struct tm buf{};
#if defined(_WIN32)
  gmtime_s(&buf, &t);
#else
  gmtime_r(&t, &buf);
#endif

  char out[24];
  std::strftime(out, sizeof(out), "%Y-%m-%dT%H:%M:%SZ", &buf);
  return out;
}

int64_t TimeUtil::parseIso8601Utc(const std::string& iso8601) {
  std::tm tm{};
  std::istringstream ss(iso8601);
  ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  if (ss.fail()) {
    return -1;
  }

#if defined(_WIN32)
  const std::time_t t = _mkgmtime(&tm);
#else
  const std::time_t t = timegm(&tm);
#endif
  if (t < 0) return -1;
  return static_cast<int64_t>(t) * 1000;
}

}  // namespace clawforge::util
