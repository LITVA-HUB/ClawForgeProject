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

  // Returns true if the request from `source` is within the rate limit.
  bool allow(const std::string& source, int64_t nowMs);

  // Clears the bucket for `source` (admin reset, e.g. after IP unblock).
  void reset(const std::string& source);

  // Returns the number of distinct sources currently tracked.
  std::size_t trackedSources() const;

  bool isEnabled() const { return enabled_; }

 private:
  bool enabled_{true};
  int maxRequests_{60};
  int windowMs_{60000};
  mutable std::mutex mu_;
  std::unordered_map<std::string, std::deque<int64_t>> buckets_;
};

}  // namespace clawforge::security
