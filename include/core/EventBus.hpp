#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace clawforge::core {

struct Event {
  uint64_t id{0};
  std::string type;
  int64_t ts{0};
  nlohmann::json data;
};

class EventBus {
 public:
  class Subscriber {
   public:
    Subscriber() = default;
    Subscriber(EventBus* bus, uint64_t cursor);

    std::optional<Event> waitNext(int timeoutMs = 1000);

   private:
    EventBus* bus_{nullptr};
    uint64_t cursor_{1};
  };

  Subscriber subscribe();
  std::vector<Event> recent(std::size_t limit = 50) const;
  void publish(std::string type, nlohmann::json data = nlohmann::json::object());

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<Event> events_;
  uint64_t nextId_{1};
  std::atomic<uint64_t> published_{0};

  std::optional<Event> waitNext(uint64_t& cursor, int timeoutMs);
};

}  // namespace clawforge::core
