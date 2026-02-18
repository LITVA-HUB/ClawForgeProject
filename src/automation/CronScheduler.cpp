#include "automation/CronScheduler.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

#include "core/Logger.hpp"
#include "util/FileUtil.hpp"
#include "util/TimeUtil.hpp"

namespace clawforge::automation {

using json = nlohmann::json;

namespace {

json mergePatch(json target, const json& patch) {
  if (!patch.is_object()) return patch;
  if (!target.is_object()) target = json::object();
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.value().is_null()) {
      target.erase(it.key());
    } else {
      target[it.key()] = mergePatch(target.contains(it.key()) ? target[it.key()] : json(), it.value());
    }
  }
  return target;
}

bool isSessionTargetValid(const std::string& value) {
  return value == "main" || value == "isolated";
}

bool isPayloadKindValid(const std::string& value) {
  return value == "systemEvent" || value == "agentTurn";
}

bool isWakeModeValid(const std::string& value) {
  return value == "now" || value == "next-heartbeat";
}

bool isDeliveryModeValid(const std::string& value) {
  return value == "none" || value == "announce";
}

std::string scheduleKindFrom(const json& src) {
  if (src.contains("schedule") && src["schedule"].is_object()) {
    const auto& schedule = src["schedule"];
    return schedule.value("kind", src.value("kind", std::string("every")));
  }
  return src.value("kind", std::string("every"));
}

int64_t scheduleEveryMsFrom(const json& src) {
  if (src.contains("schedule") && src["schedule"].is_object()) {
    const auto& schedule = src["schedule"];
    return schedule.value("everyMs", src.value("everyMs", 0LL));
  }
  return src.value("everyMs", 0LL);
}

std::string scheduleAtFrom(const json& src) {
  if (src.contains("schedule") && src["schedule"].is_object()) {
    const auto& schedule = src["schedule"];
    return schedule.value("at", src.value("at", std::string()));
  }
  return src.value("at", std::string());
}

std::string scheduleExprFrom(const json& src) {
  if (src.contains("schedule") && src["schedule"].is_object()) {
    const auto& schedule = src["schedule"];
    if (schedule.contains("expr")) return schedule.value("expr", std::string());
    return schedule.value("cron", src.value("cron", std::string()));
  }
  return src.value("cron", std::string());
}

std::string scheduleTzFrom(const json& src) {
  if (src.contains("schedule") && src["schedule"].is_object()) {
    const auto& schedule = src["schedule"];
    return schedule.value("tz", src.value("tz", std::string("UTC")));
  }
  return src.value("tz", std::string("UTC"));
}

bool parseJobSpec(const json& src, CronJob& out, const std::string& fallbackId,
                  int64_t now, bool strict, std::string& error) {
  CronJob job;
  job.id = fallbackId.empty() ? src.value("id", std::string()) : fallbackId;
  if (job.id.empty()) {
    error = "job id is required";
    return false;
  }

  job.name = src.value("name", "job-" + job.id.substr(0, 8));
  job.description = src.value("description", std::string());

  job.kind = scheduleKindFrom(src);
  job.everyMs = scheduleEveryMsFrom(src);
  job.atIso = scheduleAtFrom(src);
  job.cronExpr = scheduleExprFrom(src);
  job.tz = scheduleTzFrom(src);

  if (job.kind == "at") {
    const auto ts = util::TimeUtil::parseIso8601Utc(job.atIso);
    if (ts <= 0) {
      error = "Invalid ISO time for 'at' schedule";
      return false;
    }
  } else if (job.kind == "every") {
    if (job.everyMs <= 0) {
      error = "everyMs must be > 0";
      return false;
    }
  } else if (job.kind == "cron") {
    std::vector<int> minutes;
    std::vector<int> hours;
    std::vector<int> weekdays;
    std::string cronError;
    if (!CronScheduler::parseCronExpr(job.cronExpr, minutes, hours, weekdays, cronError)) {
      error = cronError;
      return false;
    }
  } else {
    error = "Unsupported kind. Use 'every', 'at' or 'cron'";
    return false;
  }

  json payload = (src.contains("payload") && src["payload"].is_object()) ? src["payload"] : json::object();
  job.sessionTarget = src.value("sessionTarget", std::string());
  const std::string legacySessionKey = src.value("sessionKey", std::string("main"));

  std::string payloadKind = payload.value("kind", std::string());
  if (job.sessionTarget.empty()) {
    if (payloadKind == "agentTurn") {
      job.sessionTarget = "isolated";
    } else if (!legacySessionKey.empty() && legacySessionKey != "main") {
      job.sessionTarget = "isolated";
    } else {
      job.sessionTarget = "main";
    }
  }
  if (!isSessionTargetValid(job.sessionTarget)) {
    error = "sessionTarget must be 'main' or 'isolated'";
    return false;
  }

  if (payloadKind.empty()) {
    payloadKind = (job.sessionTarget == "main") ? "systemEvent" : "agentTurn";
  }
  if (!isPayloadKindValid(payloadKind)) {
    error = "payload.kind must be 'systemEvent' or 'agentTurn'";
    return false;
  }

  if (strict) {
    if (job.sessionTarget == "main" && payloadKind != "systemEvent") {
      error = "sessionTarget='main' requires payload.kind='systemEvent'";
      return false;
    }
    if (job.sessionTarget == "isolated" && payloadKind != "agentTurn") {
      error = "sessionTarget='isolated' requires payload.kind='agentTurn'";
      return false;
    }
  }

  job.payload.kind = payloadKind;
  if (payloadKind == "systemEvent") {
    job.payload.text = payload.value("text", std::string());
    if (job.payload.text.empty()) job.payload.text = payload.value("message", std::string());
  } else {
    job.payload.text = payload.value("message", std::string());
    if (job.payload.text.empty()) job.payload.text = payload.value("text", std::string());
  }
  if (job.payload.text.empty()) {
    job.payload.text = src.value("message", std::string());
  }
  if (strict && job.payload.text.empty()) {
    error = "payload text/message is required";
    return false;
  }

  job.payload.model = payload.value("model", std::string());
  job.payload.thinking = payload.value("thinking", std::string());
  job.payload.timeoutSeconds = payload.value("timeoutSeconds", 0);

  job.message = job.payload.text;
  job.sessionKey = src.value("sessionKey", job.sessionTarget == "main" ? "main" : ("cron:" + job.id));

  job.wakeMode = src.value("wakeMode", std::string("now"));
  if (!isWakeModeValid(job.wakeMode)) {
    error = "wakeMode must be 'now' or 'next-heartbeat'";
    return false;
  }

  job.delivery.mode = job.sessionTarget == "isolated" ? "announce" : "none";
  if (src.contains("delivery")) {
    if (!src["delivery"].is_object()) {
      error = "delivery must be an object";
      return false;
    }
    const auto& delivery = src["delivery"];
    job.delivery.mode = delivery.value("mode", job.delivery.mode);
    job.delivery.channel = delivery.value("channel", std::string("last"));
    job.delivery.to = delivery.value("to", std::string());
    job.delivery.bestEffort = delivery.value("bestEffort", false);
  }
  if (!isDeliveryModeValid(job.delivery.mode)) {
    error = "delivery.mode must be 'none' or 'announce'";
    return false;
  }
  if (strict && job.sessionTarget != "isolated" && src.contains("delivery") && job.delivery.mode != "none") {
    error = "delivery is only valid for sessionTarget='isolated' (or mode='none')";
    return false;
  }

  job.agentId = src.value("agentId", std::string());
  job.enabled = src.value("enabled", true);
  if (src.contains("deleteAfterRun")) {
    job.deleteAfterRun = src.value("deleteAfterRun", false);
  } else {
    job.deleteAfterRun = (job.kind == "at");
  }

  job.consecutiveErrors = src.value("consecutiveErrors", 0);
  job.lastRunAt = src.value("lastRunAt", 0LL);
  job.lastSuccessAt = src.value("lastSuccessAt", 0LL);

  job.nextRunAt = src.value("nextRunAt", 0LL);
  if (job.nextRunAt <= 0) {
    if (job.kind == "at") {
      job.nextRunAt = util::TimeUtil::parseIso8601Utc(job.atIso);
    } else if (job.kind == "every") {
      job.nextRunAt = now + std::max<int64_t>(job.everyMs, 1000);
    } else if (job.kind == "cron") {
      job.nextRunAt = CronScheduler::computeNextCronRun(job.cronExpr, now);
    }
  }

  out = std::move(job);
  return true;
}

void appendRunRecord(const std::filesystem::path& runsDir, const std::string& jobId, const json& record) {
  util::FileUtil::ensureDir(runsDir);
  const auto path = runsDir / (jobId + ".jsonl");
  std::ofstream out(path, std::ios::out | std::ios::app);
  if (!out) return;
  out << record.dump() << "\n";
}

}  // namespace

CronScheduler::CronScheduler(std::filesystem::path stateDir, int tickMs, JobCallback callback,
                             core::EventBus& eventBus)
    : cronDir_(std::move(stateDir) / "cron"),
      jobsPath_(cronDir_ / "jobs.json"),
      runsDir_(cronDir_ / "runs"),
      tickMs_(tickMs),
      callback_(std::move(callback)),
      eventBus_(eventBus) {}

CronScheduler::~CronScheduler() { stop(); }

bool CronScheduler::init() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!util::FileUtil::ensureDir(cronDir_)) {
    return false;
  }
  util::FileUtil::ensureDir(runsDir_);
  loadUnsafe();
  return true;
}

void CronScheduler::start() {
  if (running_.exchange(true)) return;
  worker_ = std::thread(&CronScheduler::loop, this);
}

void CronScheduler::stop() {
  if (!running_.exchange(false)) return;
  if (worker_.joinable()) worker_.join();
}

std::vector<CronJob> CronScheduler::listJobs() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return jobs_;
}

nlohmann::json CronScheduler::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  const int64_t now = util::TimeUtil::nowMillis();
  int enabled = 0;
  int due = 0;
  for (const auto& job : jobs_) {
    if (!job.enabled) continue;
    ++enabled;
    if (job.nextRunAt > 0 && job.nextRunAt <= now) ++due;
  }
  return {{"ok", true},
          {"running", running_.load()},
          {"tickMs", tickMs_},
          {"jobs", {{"total", static_cast<int>(jobs_.size())}, {"enabled", enabled}, {"due", due}, {"inFlight", static_cast<int>(inFlight_.size())}}}};
}

nlohmann::json CronScheduler::getJob(const std::string& id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const CronJob& j) { return j.id == id; });
  if (it == jobs_.end()) return {{"ok", false}, {"error", "job not found"}, {"id", id}};
  return {{"ok", true}, {"job", jobToJson(*it)}};
}

nlohmann::json CronScheduler::validate(const nlohmann::json& payload) const {
  CronJob ignored;
  std::string error;
  const bool ok = parseJobSpec(payload, ignored, payload.value("id", std::string("job-validate")),
                               util::TimeUtil::nowMillis(), true, error);
  if (!ok) {
    return {{"ok", false}, {"error", error}};
  }
  return {{"ok", true}};
}

nlohmann::json CronScheduler::addJob(const nlohmann::json& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  CronJob job;
  std::string error;
  const std::string id = newId();
  if (!parseJobSpec(payload, job, id, util::TimeUtil::nowMillis(), true, error)) {
    return {{"ok", false}, {"error", error}};
  }

  jobs_.push_back(job);
  saveUnsafe();

  return {{"ok", true}, {"job", jobToJson(job)}};
}

nlohmann::json CronScheduler::updateJob(const std::string& id, const nlohmann::json& patch) {
  if (!patch.is_object()) return {{"ok", false}, {"error", "patch must be an object"}};

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const CronJob& j) { return j.id == id; });
  if (it == jobs_.end()) return {{"ok", false}, {"error", "job not found"}, {"id", id}};

  json merged = mergePatch(jobToJson(*it), patch);
  CronJob next;
  std::string error;
  if (!parseJobSpec(merged, next, id, util::TimeUtil::nowMillis(), true, error)) {
    return {{"ok", false}, {"error", error}};
  }

  *it = std::move(next);
  saveUnsafe();
  return {{"ok", true}, {"job", jobToJson(*it)}};
}

nlohmann::json CronScheduler::setEnabled(const std::string& id, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const CronJob& j) { return j.id == id; });
  if (it == jobs_.end()) return {{"ok", false}, {"error", "job not found"}, {"id", id}};

  it->enabled = enabled;
  if (enabled && it->nextRunAt <= 0) {
    const int64_t now = util::TimeUtil::nowMillis();
    if (it->kind == "at") it->nextRunAt = util::TimeUtil::parseIso8601Utc(it->atIso);
    else if (it->kind == "every") it->nextRunAt = now + std::max<int64_t>(it->everyMs, 1000);
    else if (it->kind == "cron") it->nextRunAt = computeNextCronRun(it->cronExpr, now);
  }

  saveUnsafe();
  return {{"ok", true}, {"job", jobToJson(*it)}};
}

nlohmann::json CronScheduler::runNow(const std::string& id, const std::string& mode) {
  return executeRun(id, true, mode.empty() ? "force" : mode);
}

nlohmann::json CronScheduler::listRuns(const std::string& id, int limit) const {
  const auto path = runsDir_ / (id + ".jsonl");
  if (!std::filesystem::exists(path)) {
    return {{"ok", true}, {"id", id}, {"runs", json::array()}};
  }

  std::ifstream in(path);
  if (!in) return {{"ok", false}, {"error", "cannot read runs file"}, {"id", id}};

  std::vector<json> all;
  std::string line;
  while (std::getline(in, line)) {
    auto parsed = json::parse(line, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) continue;
    all.push_back(std::move(parsed));
  }

  const int safeLimit = std::max(1, limit);
  const size_t start = all.size() > static_cast<size_t>(safeLimit)
                           ? all.size() - static_cast<size_t>(safeLimit)
                           : 0;

  json out = json::array();
  for (size_t i = start; i < all.size(); ++i) out.push_back(all[i]);
  return {{"ok", true}, {"id", id}, {"runs", out}};
}

bool CronScheduler::removeJob(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto before = jobs_.size();
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const CronJob& job) { return job.id == id; }),
              jobs_.end());

  if (jobs_.size() != before) {
    inFlight_.erase(id);
    saveUnsafe();
    return true;
  }

  return false;
}

void CronScheduler::loadUnsafe() {
  jobs_.clear();
  auto content = util::FileUtil::readText(jobsPath_);
  if (!content.has_value() || content->empty()) return;

  auto parsed = json::parse(*content, nullptr, false);
  if (!parsed.is_array()) return;

  const int64_t now = util::TimeUtil::nowMillis();
  for (const auto& j : parsed) {
    if (!j.is_object()) continue;

    CronJob job;
    std::string error;
    const std::string fallbackId = j.value("id", std::string());
    if (fallbackId.empty()) continue;

    if (!parseJobSpec(j, job, fallbackId, now, false, error)) {
      core::Logger::warn("Skipping invalid cron job " + fallbackId + ": " + error);
      continue;
    }

    jobs_.push_back(std::move(job));
  }
}

void CronScheduler::saveUnsafe() const {
  json arr = json::array();
  for (const auto& j : jobs_) {
    arr.push_back(jobToJson(j));
  }
  util::FileUtil::writeText(jobsPath_, arr.dump(2));
}

nlohmann::json CronScheduler::executeRun(const std::string& id, bool manual, const std::string& mode) {
  if (mode != "force" && mode != "due") {
    return {{"ok", false}, {"error", "mode must be 'force' or 'due'"}, {"id", id}};
  }

  CronJob job;
  const int64_t startedAt = util::TimeUtil::nowMillis();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const CronJob& j) { return j.id == id; });
    if (it == jobs_.end()) return {{"ok", false}, {"error", "job not found"}, {"id", id}};

    if (inFlight_.count(id)) {
      return {{"ok", false}, {"error", "job already running"}, {"id", id}};
    }

    if (mode == "due" && it->nextRunAt > 0 && startedAt < it->nextRunAt) {
      json skipped = {{"ok", true},
                      {"id", id},
                      {"manual", manual},
                      {"mode", mode},
                      {"ran", false},
                      {"status", "skipped"},
                      {"reason", "not-due"},
                      {"nextRunAt", it->nextRunAt}};
      appendRunRecord(runsDir_, id,
                      {{"runId", newId()},
                       {"jobId", id},
                       {"name", it->name},
                       {"status", "skipped"},
                       {"reason", "not-due"},
                       {"manual", manual},
                       {"mode", mode},
                       {"startedAt", startedAt},
                       {"endedAt", startedAt},
                       {"durationMs", 0},
                       {"nextRunAt", it->nextRunAt},
                       {"ts", util::TimeUtil::nowIso8601()}});
      return skipped;
    }

    job = *it;
    inFlight_.insert(id);
  }

  bool success = false;
  std::string callbackError;
  try {
    callback_(job);
    success = true;
  } catch (const std::exception& e) {
    callbackError = e.what();
  } catch (...) {
    callbackError = "unknown error";
  }

  const int64_t endedAt = util::TimeUtil::nowMillis();
  const int64_t durationMs = std::max<int64_t>(0, endedAt - startedAt);

  int64_t nextRunAt = 0;
  bool enabled = false;
  int consecutiveErrors = 0;
  bool removed = false;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const CronJob& j) { return j.id == id; });
    if (it != jobs_.end()) {
      it->lastRunAt = endedAt;

      if (success) {
        it->consecutiveErrors = 0;
        it->lastSuccessAt = endedAt;

        if (it->kind == "at") {
          if (it->deleteAfterRun) {
            jobs_.erase(it);
            removed = true;
          } else {
            it->enabled = false;
            it->nextRunAt = 0;
          }
        } else if (it->kind == "every") {
          it->nextRunAt = endedAt + std::max<int64_t>(it->everyMs, 1000);
        } else if (it->kind == "cron") {
          it->nextRunAt = computeNextCronRun(it->cronExpr, endedAt + 1000);
        }
      } else {
        it->consecutiveErrors += 1;

        if (it->kind == "at") {
          if (it->deleteAfterRun) {
            jobs_.erase(it);
            removed = true;
          } else {
            it->enabled = false;
            it->nextRunAt = 0;
          }
        } else {
          it->nextRunAt = endedAt + retryBackoffMs(it->consecutiveErrors);
        }
      }

      if (!removed) {
        auto jt = std::find_if(jobs_.begin(), jobs_.end(), [&](const CronJob& j) { return j.id == id; });
        if (jt != jobs_.end()) {
          nextRunAt = jt->nextRunAt;
          enabled = jt->enabled;
          consecutiveErrors = jt->consecutiveErrors;
        }
      }

      saveUnsafe();
    }

    inFlight_.erase(id);
  }

  if (success) {
    eventBus_.publish("cron_fired", {{"id", job.id},
                                      {"name", job.name},
                                      {"manual", manual},
                                      {"sessionTarget", job.sessionTarget},
                                      {"wakeMode", job.wakeMode}});
  } else {
    const std::string err = "Cron callback failed: " + callbackError;
    eventBus_.publish("error", {{"where", manual ? "CronScheduler::runNow" : "CronScheduler::loop"},
                                 {"id", job.id},
                                 {"error", err}});
    core::Logger::error(err);
  }

  appendRunRecord(runsDir_, id,
                  {{"runId", newId()},
                   {"jobId", id},
                   {"name", job.name},
                   {"status", success ? "ok" : "error"},
                   {"manual", manual},
                   {"mode", mode},
                   {"sessionTarget", job.sessionTarget},
                   {"wakeMode", job.wakeMode},
                   {"deliveryMode", job.delivery.mode},
                   {"startedAt", startedAt},
                   {"endedAt", endedAt},
                   {"durationMs", durationMs},
                   {"nextRunAt", nextRunAt},
                   {"enabled", enabled},
                   {"removed", removed},
                   {"consecutiveErrors", consecutiveErrors},
                   {"error", callbackError},
                   {"ts", util::TimeUtil::nowIso8601()}});

  if (!success) {
    return {{"ok", false},
            {"id", id},
            {"manual", manual},
            {"mode", mode},
            {"status", "error"},
            {"error", callbackError},
            {"nextRunAt", nextRunAt},
            {"enabled", enabled},
            {"removed", removed},
            {"consecutiveErrors", consecutiveErrors}};
  }

  return {{"ok", true},
          {"id", id},
          {"manual", manual},
          {"mode", mode},
          {"ran", true},
          {"status", "ok"},
          {"nextRunAt", nextRunAt},
          {"enabled", enabled},
          {"removed", removed}};
}

void CronScheduler::loop() {
  while (running_.load()) {
    std::vector<std::string> dueIds;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      const int64_t now = util::TimeUtil::nowMillis();
      for (const auto& job : jobs_) {
        if (!job.enabled) continue;
        if (job.nextRunAt <= 0) continue;
        if (now < job.nextRunAt) continue;
        if (inFlight_.count(job.id)) continue;
        dueIds.push_back(job.id);
      }
    }

    for (const auto& id : dueIds) {
      (void)executeRun(id, false, "force");
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(tickMs_));
  }
}

std::string CronScheduler::newId() {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream ss;
  ss << std::hex << dist(rng);
  return ss.str();
}

int64_t CronScheduler::retryBackoffMs(int consecutiveErrors) {
  if (consecutiveErrors <= 0) return 0;
  static const int64_t ladder[] = {30000, 60000, 300000, 900000, 3600000};
  const int ladderSize = static_cast<int>(sizeof(ladder) / sizeof(ladder[0]));
  const int idx = std::min<int>(ladderSize - 1, consecutiveErrors - 1);
  return ladder[idx];
}

nlohmann::json CronScheduler::jobToJson(const CronJob& j) {
  json payload = {{"kind", j.payload.kind},
                  {"model", j.payload.model},
                  {"thinking", j.payload.thinking},
                  {"timeoutSeconds", j.payload.timeoutSeconds}};
  if (j.payload.kind == "systemEvent") payload["text"] = j.payload.text;
  else payload["message"] = j.payload.text;
  payload["text"] = j.payload.text;      // compatibility
  payload["message"] = j.payload.text;   // compatibility

  return {{"id", j.id},
          {"name", j.name},
          {"description", j.description},
          {"kind", j.kind},
          {"everyMs", j.everyMs},
          {"at", j.atIso},
          {"cron", j.cronExpr},
          {"tz", j.tz},
          {"schedule", {{"kind", j.kind}, {"everyMs", j.everyMs}, {"at", j.atIso}, {"expr", j.cronExpr}, {"tz", j.tz}}},
          {"nextRunAt", j.nextRunAt},
          {"sessionKey", j.sessionKey},
          {"message", j.message},
          {"sessionTarget", j.sessionTarget},
          {"wakeMode", j.wakeMode},
          {"agentId", j.agentId},
          {"deleteAfterRun", j.deleteAfterRun},
          {"payload", payload},
          {"delivery", {{"mode", j.delivery.mode}, {"channel", j.delivery.channel}, {"to", j.delivery.to}, {"bestEffort", j.delivery.bestEffort}}},
          {"enabled", j.enabled},
          {"consecutiveErrors", j.consecutiveErrors},
          {"lastRunAt", j.lastRunAt},
          {"lastSuccessAt", j.lastSuccessAt}};
}

bool CronScheduler::parseCronExpr(const std::string& expr, std::vector<int>& minutes,
                                  std::vector<int>& hours, std::vector<int>& weekdays,
                                  std::string& error) {
  std::istringstream ss(expr);
  std::string m;
  std::string h;
  std::string w;
  if (!(ss >> m >> h >> w)) {
    error = "Invalid cron expression. Expected: '<minute> <hour> <dayOfWeek>'";
    return false;
  }

  std::string extra;
  if (ss >> extra) {
    error = "Invalid cron expression. Too many fields";
    return false;
  }

  if (!parseCronField(m, 0, 59, minutes)) {
    error = "Invalid cron minute field";
    return false;
  }
  if (!parseCronField(h, 0, 23, hours)) {
    error = "Invalid cron hour field";
    return false;
  }
  if (!parseCronField(w, 0, 6, weekdays)) {
    error = "Invalid cron dayOfWeek field (0=Sun..6=Sat)";
    return false;
  }

  return true;
}

bool CronScheduler::parseCronField(const std::string& field, int min, int max, std::vector<int>& out) {
  out.clear();
  std::set<int> uniq;

  if (field == "*") {
    for (int i = min; i <= max; ++i) uniq.insert(i);
  } else {
    std::istringstream ss(field);
    std::string token;
    while (std::getline(ss, token, ',')) {
      if (token.empty()) return false;
      for (char c : token) {
        if (c < '0' || c > '9') return false;
      }
      const int v = std::stoi(token);
      if (v < min || v > max) return false;
      uniq.insert(v);
    }
  }

  out.assign(uniq.begin(), uniq.end());
  return !out.empty();
}

int64_t CronScheduler::computeNextCronRun(const std::string& expr, int64_t fromMillis) {
  std::vector<int> minutes;
  std::vector<int> hours;
  std::vector<int> weekdays;
  std::string error;
  if (!parseCronExpr(expr, minutes, hours, weekdays, error)) {
    return -1;
  }

  using clock = std::chrono::system_clock;
  auto tp = clock::time_point(std::chrono::milliseconds(fromMillis));
  auto secs = std::chrono::time_point_cast<std::chrono::seconds>(tp);
  auto rounded = secs + std::chrono::minutes(1);

  for (int i = 0; i < 60 * 24 * 14; ++i) {
    const auto candidate = rounded + std::chrono::minutes(i);
    const auto t = clock::to_time_t(candidate);

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif

    const bool minuteOk = std::find(minutes.begin(), minutes.end(), tm.tm_min) != minutes.end();
    const bool hourOk = std::find(hours.begin(), hours.end(), tm.tm_hour) != hours.end();
    const bool weekdayOk = std::find(weekdays.begin(), weekdays.end(), tm.tm_wday) != weekdays.end();

    if (minuteOk && hourOk && weekdayOk) {
      return std::chrono::duration_cast<std::chrono::milliseconds>(candidate.time_since_epoch()).count();
    }
  }

  return -1;
}

}  // namespace clawforge::automation
