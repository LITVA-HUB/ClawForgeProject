#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/EventBus.hpp"

namespace clawforge::orchestration {

struct TaskConfig {
  bool enabled{true};
  int maxQueue{256};
  int defaultTimeoutMs{30000};
};

struct TaskEvent {
  int64_t seq{0};
  std::string type;
  std::string status;
  int64_t atMs{0};
  nlohmann::json details{nlohmann::json::object()};
};

struct TaskRecord {
  std::string id;
  std::string status{"queued"};  // queued|running|cancelling|done|failed|cancelled|timeout
  std::string channel{"api"};
  std::string peerId;
  std::string text;
  bool systemEvent{false};
  int timeoutMs{30000};
  nlohmann::json contextPolicy{nlohmann::json::object()};
  nlohmann::json toolsPolicy{nlohmann::json::object()};
  std::vector<TaskEvent> events;
  int64_t nextEventSeq{1};
  std::string result;
  std::string error;
  int64_t createdAtMs{0};
  int64_t startedAtMs{0};
  int64_t finishedAtMs{0};
};

class TaskQueue {
 public:
  using ExecuteFn = std::function<std::string(const TaskRecord&)>;

  TaskQueue(std::filesystem::path stateDir, TaskConfig config, core::EventBus& eventBus, ExecuteFn execFn);
  ~TaskQueue();

  bool init();
  void start();
  void stop();

  nlohmann::json enqueue(const nlohmann::json& body);
  nlohmann::json cancel(const std::string& id);
  nlohmann::json get(const std::string& id) const;
  nlohmann::json list() const;
  nlohmann::json events(const std::string& id, int limit, int64_t afterSeq) const;

 private:
  std::filesystem::path storePath_;
  TaskConfig config_;
  core::EventBus& eventBus_;
  ExecuteFn execute_;

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::string> queue_;
  std::unordered_map<std::string, TaskRecord> tasks_;
  std::unordered_map<std::string, bool> cancelFlags_;
  std::thread worker_;
  std::atomic<bool> running_{false};

  bool saveLocked() const;
  bool loadLocked();
  static std::string genTaskId();
  static nlohmann::json toJson(const TaskRecord& t);
  static nlohmann::json eventToJson(const TaskEvent& ev);
  static void appendEvent(TaskRecord& t, const std::string& type, const std::string& status,
                          const nlohmann::json& details = nlohmann::json::object());
  void workerLoop();
};

}  // namespace clawforge::orchestration
