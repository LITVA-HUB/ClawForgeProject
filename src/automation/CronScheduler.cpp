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

CronScheduler::CronScheduler(std::filesystem::path stateDir, int tickMs, JobCallback callback,
                             core::EventBus& eventBus)
    : cronDir_(std::move(stateDir) / "cron"),
      jobsPath_(cronDir_ / "jobs.json"),
      tickMs_(tickMs),
      callback_(std::move(callback)),
      eventBus_(eventBus) {}

CronScheduler::~CronScheduler() { stop(); }

bool CronScheduler::init() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!util::FileUtil::ensureDir(cronDir_)) {
    return false;
  }
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

nlohmann::json CronScheduler::validate(const nlohmann::json& payload) const {
  const std::string kind = payload.value("kind", "every");
  if (kind == "at") {
    const auto at = payload.value("at", "");
    const auto ts = util::TimeUtil::parseIso8601Utc(at);
    if (ts <= 0) {
      return {{"ok", false}, {"error", "Invalid ISO time for 'at' schedule"}};
    }
    return {{"ok", true}};
  }

  if (kind == "every") {
    const int64_t everyMs = payload.value("everyMs", 0LL);
    if (everyMs <= 0) {
      return {{"ok", false}, {"error", "everyMs must be > 0"}};
    }
    return {{"ok", true}};
  }

  if (kind == "cron") {
    const std::string expr = payload.value("cron", "");
    std::vector<int> minutes;
    std::vector<int> hours;
    std::vector<int> weekdays;
    std::string error;
    if (!parseCronExpr(expr, minutes, hours, weekdays, error)) {
      return {{"ok", false}, {"error", error}};
    }
    return {{"ok", true}};
  }

  return {{"ok", false}, {"error", "Unsupported kind. Use 'every', 'at' or 'cron'"}};
}

nlohmann::json CronScheduler::addJob(const nlohmann::json& payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto validation = validate(payload);
  if (!validation.value("ok", false)) {
    return validation;
  }

  CronJob job;
  job.id = newId();
  job.name = payload.value("name", "job-" + job.id.substr(0, 8));
  job.kind = payload.value("kind", "every");
  job.everyMs = payload.value("everyMs", 0LL);
  job.atIso = payload.value("at", "");
  job.cronExpr = payload.value("cron", "");
  job.sessionKey = payload.value("sessionKey", "main");
  job.message = payload.value("message", "");
  job.enabled = payload.value("enabled", true);

  const int64_t now = util::TimeUtil::nowMillis();
  if (job.kind == "at") {
    job.nextRunAt = util::TimeUtil::parseIso8601Utc(job.atIso);
  } else if (job.kind == "every") {
    job.nextRunAt = now + job.everyMs;
  } else if (job.kind == "cron") {
    job.nextRunAt = computeNextCronRun(job.cronExpr, now);
  }

  jobs_.push_back(job);
  saveUnsafe();

  return {{"ok", true},
          {"job",
           {{"id", job.id},
            {"name", job.name},
            {"kind", job.kind},
            {"everyMs", job.everyMs},
            {"at", job.atIso},
            {"cron", job.cronExpr},
            {"nextRunAt", job.nextRunAt},
            {"sessionKey", job.sessionKey},
            {"message", job.message},
            {"enabled", job.enabled}}}};
}

nlohmann::json CronScheduler::runNow(const std::string& id) {
  CronJob job;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const CronJob& j) { return j.id == id; });
    if (it == jobs_.end()) {
      return {{"ok", false}, {"error", "job not found"}};
    }

    job = *it;
    const int64_t now = util::TimeUtil::nowMillis();
    if (it->kind == "at") {
      it->enabled = false;
    } else if (it->kind == "every") {
      it->nextRunAt = now + std::max<int64_t>(it->everyMs, 1000);
    } else if (it->kind == "cron") {
      it->nextRunAt = computeNextCronRun(it->cronExpr, now);
    }
    saveUnsafe();
  }

  try {
    callback_(job);
    eventBus_.publish("cron_fired", {{"id", job.id}, {"name", job.name}, {"manual", true}});
  } catch (const std::exception& e) {
    const std::string err = std::string("Cron callback failed: ") + e.what();
    eventBus_.publish("error", {{"where", "CronScheduler::runNow"}, {"id", job.id}, {"error", err}});
    return {{"ok", false}, {"error", err}};
  }

  return {{"ok", true}, {"id", id}};
}

bool CronScheduler::removeJob(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto before = jobs_.size();
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(), [&](const CronJob& job) { return job.id == id; }),
              jobs_.end());

  if (jobs_.size() != before) {
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

  for (const auto& j : parsed) {
    CronJob job;
    job.id = j.value("id", "");
    job.name = j.value("name", "");
    job.kind = j.value("kind", "every");
    job.everyMs = j.value("everyMs", 0LL);
    job.atIso = j.value("at", "");
    job.cronExpr = j.value("cron", "");
    job.nextRunAt = j.value("nextRunAt", 0LL);
    job.sessionKey = j.value("sessionKey", "main");
    job.message = j.value("message", "");
    job.enabled = j.value("enabled", true);
    if (!job.id.empty()) {
      jobs_.push_back(std::move(job));
    }
  }
}

void CronScheduler::saveUnsafe() const {
  json arr = json::array();
  for (const auto& j : jobs_) {
    arr.push_back({{"id", j.id},
                   {"name", j.name},
                   {"kind", j.kind},
                   {"everyMs", j.everyMs},
                   {"at", j.atIso},
                   {"cron", j.cronExpr},
                   {"nextRunAt", j.nextRunAt},
                   {"sessionKey", j.sessionKey},
                   {"message", j.message},
                   {"enabled", j.enabled}});
  }
  util::FileUtil::writeText(jobsPath_, arr.dump(2));
}

void CronScheduler::loop() {
  while (running_.load()) {
    std::vector<CronJob> due;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      const int64_t now = util::TimeUtil::nowMillis();
      bool changed = false;

      for (auto& job : jobs_) {
        if (!job.enabled) continue;
        if (job.nextRunAt <= 0) continue;
        if (now < job.nextRunAt) continue;

        due.push_back(job);
        changed = true;

        if (job.kind == "at") {
          job.enabled = false;
        } else if (job.kind == "every") {
          job.nextRunAt = now + std::max<int64_t>(job.everyMs, 1000);
        } else if (job.kind == "cron") {
          job.nextRunAt = computeNextCronRun(job.cronExpr, now + 1000);
        }
      }

      if (changed) {
        saveUnsafe();
      }
    }

    for (const auto& job : due) {
      try {
        callback_(job);
        eventBus_.publish("cron_fired", {{"id", job.id}, {"name", job.name}, {"manual", false}});
      } catch (const std::exception& e) {
        const std::string err = std::string("Cron callback failed: ") + e.what();
        eventBus_.publish("error", {{"where", "CronScheduler::loop"}, {"id", job.id}, {"error", err}});
        core::Logger::error(err);
      }
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
