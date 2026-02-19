#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "agent/AgentEngine.hpp"
#include "automation/CronScheduler.hpp"
#include "browser/BrowserRelay.hpp"
#include "core/AuditTrail.hpp"
#include "core/Config.hpp"
#include "core/EventBus.hpp"
#include "orchestration/TaskQueue.hpp"
#include "security/RateLimiter.hpp"
#include "session/SessionStore.hpp"
#include "tools/ToolRegistry.hpp"

namespace clawforge::http {

class HttpServer {
 public:
  HttpServer(std::string host, int port, agent::AgentEngine& agent, session::SessionStore& sessions,
             tools::ToolRegistry& tools, browser::BrowserRelay& browser,
             orchestration::TaskQueue& tasks, automation::CronScheduler& cron,
             core::EventBus& events, core::ApiConfig apiConfig,
             core::GatewayAuthConfig authConfig, core::RateLimitConfig rateLimitConfig,
             core::AuditConfig auditConfig, int64_t startedAtMs);

  bool start();
  void stop();

 private:
  std::string host_;
  int port_;

  agent::AgentEngine& agent_;
  session::SessionStore& sessions_;
  tools::ToolRegistry& tools_;
  browser::BrowserRelay& browser_;
  orchestration::TaskQueue& tasks_;
  automation::CronScheduler& cron_;
  [[maybe_unused]] core::EventBus& events_;
  core::ApiConfig apiConfig_;
  core::GatewayAuthConfig authConfig_;
  security::RateLimiter rateLimiter_;
  core::AuditTrail audit_;
  std::filesystem::path auditFilePath_;
  int64_t startedAtMs_{0};

  httplib::Server server_;

  void setupRoutes();
  std::string deriveApiSessionKey(const nlohmann::json& body) const;
  static int parsePositiveLimit(const httplib::Request& req, const std::string& key, int fallback, int maxValue);
  nlohmann::json readAuditTail(int limit) const;
  nlohmann::json readEventTail(int limit) const;
};

}  // namespace clawforge::http
