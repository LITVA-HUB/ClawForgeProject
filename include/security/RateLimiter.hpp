#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace clawforge::security {

class RateLimiter {
 public:
  RateLimiter(bool enabled, int maxRequests, int windowMs);
  bool allow(const std::string& source, int64_t nowMs);

 private:
  bool enabled_{true};
  int maxRequests_{60};
  int windowMs_{60000};
  std::mutex mu_;
  std::unordered_map<std::string, std::deque<int64_t>> buckets_;
};

}  // namespace clawforge::security
