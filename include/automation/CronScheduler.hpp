#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/EventBus.hpp"

namespace clawforge::automation {

struct CronJob {
  std::string id;
  std::string name;
  std::string kind{"every"};  // every | at | cron
  int64_t everyMs{0};
  std::string atIso;
  std::string cronExpr;         // "<minute> <hour> <dayOfWeek>"
  int64_t nextRunAt{0};
  std::string sessionKey{"main"};
  std::string message;
  bool enabled{true};
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
  nlohmann::json addJob(const nlohmann::json& payload);
  nlohmann::json validate(const nlohmann::json& payload) const;
  nlohmann::json runNow(const std::string& id);
  bool removeJob(const std::string& id);

 private:
  std::filesystem::path cronDir_;
  std::filesystem::path jobsPath_;
  int tickMs_{1000};
  JobCallback callback_;
  core::EventBus& eventBus_;

  mutable std::mutex mutex_;
  std::vector<CronJob> jobs_;

  std::atomic<bool> running_{false};
  std::thread worker_;

  void loadUnsafe();
  void saveUnsafe() const;
  void loop();
  static std::string newId();

  static bool parseCronExpr(const std::string& expr, std::vector<int>& minutes,
                            std::vector<int>& hours, std::vector<int>& weekdays,
                            std::string& error);
  static bool parseCronField(const std::string& field, int min, int max, std::vector<int>& out);
  static int64_t computeNextCronRun(const std::string& expr, int64_t fromMillis);
};

}  // namespace clawforge::automation
