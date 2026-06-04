#include "security/RateLimiter.hpp"

namespace clawforge::security {

RateLimiter::RateLimiter(bool enabled, int maxRequests, int windowMs)
    : enabled_(enabled), maxRequests_(maxRequests), windowMs_(windowMs) {}

bool RateLimiter::allow(const std::string& source, int64_t nowMs) {
  if (!enabled_) return true;
  if (source.empty()) return true;

  std::lock_guard<std::mutex> lock(mu_);
  auto& q = buckets_[source];
  while (!q.empty() && nowMs - q.front() > windowMs_) q.pop_front();
  if (static_cast<int>(q.size()) >= maxRequests_) return false;
  q.push_back(nowMs);
  return true;
}

void RateLimiter::reset(const std::string& source) {
  std::lock_guard<std::mutex> lock(mu_);
  buckets_.erase(source);
}

std::size_t RateLimiter::trackedSources() const {
  std::lock_guard<std::mutex> lock(mu_);
  return buckets_.size();
}

}  // namespace clawforge::security
