#include "orchestration/TaskQueue.hpp"

#include <algorithm>
#include <chrono>

#include "util/FileUtil.hpp"
#include "util/TimeUtil.hpp"

namespace clawforge::orchestration {

using json = nlohmann::json;

namespace {

std::optional<json> normalizeContextPolicy(const json& input, std::string* errorCode) {
  if (input.is_null()) return json::object();
  if (!input.is_object()) {
    if (errorCode) *errorCode = "invalid_context_policy";
    return std::nullopt;
  }
  json out = json::object();
  if (input.contains("historyLimit")) {
    if (!input["historyLimit"].is_number_integer()) {
      if (errorCode) *errorCode = "invalid_context_history_limit";
      return std::nullopt;
    }
    out["historyLimit"] = std::clamp(input["historyLimit"].get<int>(), 1, 200);
  }
  if (input.contains("carryover")) {
    if (!input["carryover"].is_string()) {
      if (errorCode) *errorCode = "invalid_context_carryover";
      return std::nullopt;
    }
    const auto mode = input["carryover"].get<std::string>();
    if (mode != "inherit" && mode != "minimal" && mode != "none") {
      if (errorCode) *errorCode = "invalid_context_carryover";
      return std::nullopt;
    }
    out["carryover"] = mode;
  }
  return out;
}

std::optional<json> normalizeToolsPolicy(const json& input, std::string* errorCode) {
  if (input.is_null()) return json::object();
  if (!input.is_object()) {
    if (errorCode) *errorCode = "invalid_tools_policy";
    return std::nullopt;
  }

  const auto parseList = [&](const char* field, const char* err) -> std::optional<std::vector<std::string>> {
    if (!input.contains(field)) return std::vector<std::string>{};
    if (!input[field].is_array()) {
      if (errorCode) *errorCode = err;
      return std::nullopt;
    }
    std::vector<std::string> out;
    for (const auto& item : input[field]) {
      if (!item.is_string() || item.get<std::string>().empty()) continue;
      const auto name = item.get<std::string>();
      if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
    }
    if (input[field].size() > 0 && out.empty()) {
      if (errorCode) *errorCode = err;
      return std::nullopt;
    }
    return out;
  };

  const auto allow = parseList("allow", "invalid_tool_allow");
  if (!allow.has_value()) return std::nullopt;
  const auto deny = parseList("deny", "invalid_tool_deny");
  if (!deny.has_value()) return std::nullopt;

  json out = json::object();
  if (!allow->empty()) out["allow"] = *allow;
  if (!deny->empty()) out["deny"] = *deny;
  return out;
}

}  // namespace

TaskQueue::TaskQueue(std::filesystem::path stateDir, TaskConfig config, core::EventBus& eventBus,
                     ExecuteFn execFn)
    : storePath_(std::move(stateDir) / "tasks" / "tasks.json"),
      config_(std::move(config)),
      eventBus_(eventBus),
      execute_(std::move(execFn)) {}

TaskQueue::~TaskQueue() { stop(); }

bool TaskQueue::init() {
  std::lock_guard<std::mutex> lock(mu_);
  return loadLocked() && saveLocked();
}

void TaskQueue::start() {
  if (!config_.enabled || running_.exchange(true)) return;
  worker_ = std::thread(&TaskQueue::workerLoop, this);
}

void TaskQueue::stop() {
  if (!running_.exchange(false)) return;
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

std::string TaskQueue::genTaskId() {
  return "task-" + std::to_string(util::TimeUtil::nowMillis());
}

json TaskQueue::toJson(const TaskRecord& t) {
  json out = {{"id", t.id},
              {"status", t.status},
              {"channel", t.channel},
              {"peerId", t.peerId},
              {"text", t.text},
              {"systemEvent", t.systemEvent},
              {"timeoutMs", t.timeoutMs},
              {"result", t.result},
              {"error", t.error},
              {"createdAtMs", t.createdAtMs},
              {"startedAtMs", t.startedAtMs},
              {"finishedAtMs", t.finishedAtMs}};
  if (!t.contextPolicy.empty()) out["context"] = t.contextPolicy;
  if (!t.toolsPolicy.empty()) out["tools"] = t.toolsPolicy;
  return out;
}

bool TaskQueue::saveLocked() const {
  json arr = json::array();
  for (const auto& [_, task] : tasks_) arr.push_back(toJson(task));
  return util::FileUtil::writeText(storePath_, json({{"tasks", arr}}).dump(2));
}

bool TaskQueue::loadLocked() {
  tasks_.clear();
  queue_.clear();

  auto content = util::FileUtil::readText(storePath_);
  if (!content.has_value()) return true;

  const auto doc = json::parse(*content, nullptr, false);
  if (doc.is_discarded() || !doc.contains("tasks") || !doc["tasks"].is_array()) return false;

  for (const auto& row : doc["tasks"]) {
    TaskRecord t;
    t.id = row.value("id", "");
    if (t.id.empty()) continue;
    t.status = row.value("status", "queued");
    t.channel = row.value("channel", "api");
    t.peerId = row.value("peerId", "");
    t.text = row.value("text", "");
    t.systemEvent = row.value("systemEvent", false);
    t.timeoutMs = row.value("timeoutMs", config_.defaultTimeoutMs);
    t.contextPolicy = row.contains("context") && row["context"].is_object() ? row["context"] : json::object();
    t.toolsPolicy = row.contains("tools") && row["tools"].is_object() ? row["tools"] : json::object();
    t.result = row.value("result", "");
    t.error = row.value("error", "");
    t.createdAtMs = row.value("createdAtMs", util::TimeUtil::nowMillis());
    t.startedAtMs = row.value("startedAtMs", 0LL);
    t.finishedAtMs = row.value("finishedAtMs", 0LL);

    if (t.status == "queued") {
      queue_.push_back(t.id);
    } else if (t.status == "running") {
      t.status = "cancelled";
      t.error = "Recovered after restart: previous run interrupted";
      t.finishedAtMs = util::TimeUtil::nowMillis();
    }
    tasks_[t.id] = std::move(t);
  }

  return true;
}

json TaskQueue::enqueue(const json& body) {
  std::lock_guard<std::mutex> lock(mu_);
  if (static_cast<int>(queue_.size()) >= config_.maxQueue) {
    return {{"ok", false}, {"error", "task queue is full"}};
  }

  TaskRecord t;
  t.id = genTaskId();
  while (tasks_.contains(t.id)) {
    t.id = genTaskId() + "-1";
  }
  t.channel = body.value("channel", "api");
  t.peerId = body.value("peerId", "");
  t.text = body.value("text", "");
  t.systemEvent = body.value("systemEvent", false);
  t.timeoutMs = std::max(1000, body.value("timeoutMs", config_.defaultTimeoutMs));
  t.createdAtMs = util::TimeUtil::nowMillis();

  std::string policyError;
  if (body.contains("context")) {
    const auto contextPolicy = normalizeContextPolicy(body["context"], &policyError);
    if (!contextPolicy.has_value()) return {{"ok", false}, {"error", policyError}};
    t.contextPolicy = *contextPolicy;
  }
  if (body.contains("tools")) {
    const auto toolsPolicy = normalizeToolsPolicy(body["tools"], &policyError);
    if (!toolsPolicy.has_value()) return {{"ok", false}, {"error", policyError}};
    t.toolsPolicy = *toolsPolicy;
  }

  if (t.text.empty()) return {{"ok", false}, {"error", "text is required"}};

  tasks_[t.id] = t;
  queue_.push_back(t.id);
  saveLocked();
  cv_.notify_one();
  eventBus_.publish("task_enqueued", {{"id", t.id}, {"channel", t.channel}, {"peerId", t.peerId}});
  return {{"ok", true}, {"task", toJson(t)}};
}

json TaskQueue::cancel(const std::string& id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = tasks_.find(id);
  if (it == tasks_.end()) return {{"ok", false}, {"error", "task not found"}};

  auto& task = it->second;
  if (task.status == "queued") {
    task.status = "cancelled";
    task.finishedAtMs = util::TimeUtil::nowMillis();
    task.error = "Cancelled by user";
    queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
  } else if (task.status == "running") {
    cancelFlags_[id] = true;
  }
  saveLocked();
  eventBus_.publish("task_cancel", {{"id", id}, {"status", task.status}});
  return {{"ok", true}, {"task", toJson(task)}};
}

json TaskQueue::get(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = tasks_.find(id);
  if (it == tasks_.end()) return {{"ok", false}, {"error", "task not found"}};
  return {{"ok", true}, {"task", toJson(it->second)}};
}

json TaskQueue::list() const {
  std::lock_guard<std::mutex> lock(mu_);
  json arr = json::array();
  for (const auto& [_, t] : tasks_) arr.push_back(toJson(t));
  return {{"ok", true}, {"tasks", arr}, {"queued", queue_.size()}};
}

void TaskQueue::workerLoop() {
  while (running_.load()) {
    std::string id;
    {
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait_for(lock, std::chrono::milliseconds(250), [&] { return !running_.load() || !queue_.empty(); });
      if (!running_.load()) return;
      if (queue_.empty()) continue;
      id = queue_.front();
      queue_.pop_front();

      auto it = tasks_.find(id);
      if (it == tasks_.end()) continue;
      it->second.status = "running";
      it->second.startedAtMs = util::TimeUtil::nowMillis();
      saveLocked();
    }

    TaskRecord copy;
    {
      std::lock_guard<std::mutex> lock(mu_);
      copy = tasks_[id];
    }

    std::string result;
    std::string status = "done";
    std::string error;
    try {
      const auto started = util::TimeUtil::nowMillis();
      result = execute_(copy);
      const auto took = util::TimeUtil::nowMillis() - started;
      if (took > copy.timeoutMs) {
        status = "timeout";
        error = "Task exceeded timeoutMs";
      }
    } catch (const std::exception& e) {
      status = "failed";
      error = e.what();
    }

    {
      std::lock_guard<std::mutex> lock(mu_);
      auto it = tasks_.find(id);
      if (it == tasks_.end()) continue;
      if (cancelFlags_[id]) {
        status = "cancelled";
        error = "Cancelled while running";
        cancelFlags_.erase(id);
      }
      it->second.status = status;
      it->second.result = result;
      it->second.error = error;
      it->second.finishedAtMs = util::TimeUtil::nowMillis();
      saveLocked();
    }

    eventBus_.publish("task_done", {{"id", id}, {"status", status}, {"error", error}});
  }
}

}  // namespace clawforge::orchestration
