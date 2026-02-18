#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/EventBus.hpp"

namespace clawforge::automation {

struct CronPayload {
  std::string kind{"systemEvent"};  // systemEvent | agentTurn
  std::string text;
  std::string model;
  std::string thinking;
  int timeoutSeconds{0};
};

struct CronDelivery {
  std::string mode{"none"};  // none | announce
  std::string channel{"last"};
  std::string to;
  bool bestEffort{false};
};

struct CronJob {
  std::string id;
  std::string name;
  std::string description;
  std::string kind{"every"};  // every | at | cron
  int64_t everyMs{0};
  std::string atIso;
  std::string cronExpr;         // "<minute> <hour> <dayOfWeek>"
  std::string tz;
  int64_t nextRunAt{0};

  // legacy compatibility fields
  std::string sessionKey{"main"};
  std::string message;

  // stage14+ parity-ish fields
  std::string sessionTarget{"main"};   // main | isolated
  std::string wakeMode{"now"};         // now | next-heartbeat
  std::string agentId;
  bool deleteAfterRun{false};
  CronPayload payload;
  CronDelivery delivery;

  bool enabled{true};
  int consecutiveErrors{0};
  int64_t lastRunAt{0};
  int64_t lastSuccessAt{0};
};

using JobCallback = std::function<void(const CronJob&)>;

class CronScheduler {
 public:
  CronScheduler(std::filesystem::path stateDir, int tickMs, JobCallback callback, core::EventBus& eventBus);
  ~CronScheduler();

  bool init();
  void start();
  void stop();

  std::vector<CronJob> listJobs() const;
  nlohmann::json status() const;
  nlohmann::json getJob(const std::string& id) const;
  nlohmann::json addJob(const nlohmann::json& payload);
  nlohmann::json validate(const nlohmann::json& payload) const;
  nlohmann::json updateJob(const std::string& id, const nlohmann::json& patch);
  nlohmann::json setEnabled(const std::string& id, bool enabled);
  nlohmann::json runNow(const std::string& id, const std::string& mode = "force");
  nlohmann::json listRuns(const std::string& id, int limit = 20) const;
  bool removeJob(const std::string& id);

  static bool parseCronExpr(const std::string& expr, std::vector<int>& minutes,
                            std::vector<int>& hours, std::vector<int>& weekdays,
                            std::string& error);
  static bool parseCronField(const std::string& field, int min, int max, std::vector<int>& out);
  static int64_t computeNextCronRun(const std::string& expr, int64_t fromMillis);

 private:
  std::filesystem::path cronDir_;
  std::filesystem::path jobsPath_;
  std::filesystem::path runsDir_;
  int tickMs_{1000};
  JobCallback callback_;
  core::EventBus& eventBus_;

  mutable std::mutex mutex_;
  std::vector<CronJob> jobs_;
  std::set<std::string> inFlight_;

  std::atomic<bool> running_{false};
  std::thread worker_;

  void loadUnsafe();
  void saveUnsafe() const;
  void loop();
  nlohmann::json executeRun(const std::string& id, bool manual, const std::string& mode);

  static std::string newId();
  static int64_t retryBackoffMs(int consecutiveErrors);
  static nlohmann::json jobToJson(const CronJob& job);
};

}  // namespace clawforge::automation
