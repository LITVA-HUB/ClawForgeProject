#include "core/EventBus.hpp"

#include <chrono>

#include "util/TimeUtil.hpp"

namespace clawforge::core {

EventBus::Subscriber::Subscriber(EventBus* bus, uint64_t cursor) : bus_(bus), cursor_(cursor) {}

std::optional<Event> EventBus::Subscriber::waitNext(int timeoutMs) {
  if (!bus_) return std::nullopt;
  return bus_->waitNext(cursor_, timeoutMs);
}

EventBus::Subscriber EventBus::subscribe() {
  std::lock_guard<std::mutex> lock(mutex_);
  return Subscriber(this, nextId_);
}

void EventBus::publish(std::string type, nlohmann::json data) {
  Event event;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    event.id = nextId_++;
    event.type = std::move(type);
    event.ts = util::TimeUtil::nowMillis();
    event.data = std::move(data);
    events_.push_back(event);
    if (events_.size() > 1000) {
      events_.erase(events_.begin(), events_.begin() + static_cast<long>(events_.size() - 1000));
    }
    published_.store(event.id, std::memory_order_relaxed);
  }
  cv_.notify_all();
}

std::optional<Event> EventBus::waitNext(uint64_t& cursor, int timeoutMs) {
  std::unique_lock<std::mutex> lock(mutex_);

  auto hasEvent = [&]() {
    if (events_.empty()) return false;
    return cursor <= events_.back().id;
  };

  if (!hasEvent()) {
    if (timeoutMs <= 0) {
      cv_.wait(lock, hasEvent);
    } else {
      cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), hasEvent);
    }
  }

  if (events_.empty()) return std::nullopt;

  if (cursor < events_.front().id) {
    cursor = events_.front().id;
  }

  for (const auto& event : events_) {
    if (event.id >= cursor) {
      cursor = event.id + 1;
      return event;
    }
  }

  return std::nullopt;
}

}  // namespace clawforge::core
