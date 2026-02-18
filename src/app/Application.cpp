#include "app/Application.hpp"

#include <filesystem>

#include "core/Logger.hpp"
#include "tools/BuiltinTools.hpp"
#include "util/FileUtil.hpp"
#include "util/TimeUtil.hpp"

namespace clawforge::app {

Application::Application(core::AppConfig config, std::string uiLang)
    : config_(std::move(config)),
      uiLang_(std::move(uiLang)),
      sessions_(config_.stateDir),
      pairing_(config_.stateDir) {}

int Application::run() {
  const bool ru = (uiLang_ == "ru");
  core::Logger::info(ru ? "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" : "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  core::Logger::info(ru ? "🚀 Запуск ClawForge..." : "🚀 Starting ClawForge...");
  core::Logger::info((ru ? "📁 Рабочая папка: " : "📁 Workspace: ") + config_.workspace.string());
  core::Logger::info((ru ? "🗂️  State папка: " : "🗂️  State dir: ") + config_.stateDir.string());
  core::Logger::info((ru ? "🌐 HTTP: " : "🌐 HTTP: ") + config_.http.host + ":" + std::to_string(config_.http.port));
  if (!util::FileUtil::ensureDir(config_.workspace)) {
    core::Logger::error("Cannot create workspace: " + config_.workspace.string());
    return 1;
  }

  if (!util::FileUtil::ensureDir(config_.stateDir)) {
    core::Logger::error("Cannot create state dir: " + config_.stateDir.string());
    return 1;
  }

  if (!sessions_.init()) {
    core::Logger::error("Cannot initialize session store");
    return 1;
  }

  tools::registerBuiltinTools(tools_, config_.workspace);
  tools_.setPolicy(config_.toolsPolicy);

  llm_ = llm::makeLlmClient(config_.model);
  agent_ = std::make_unique<agent::AgentEngine>(sessions_, tools_, *llm_, config_.model,
                                                 config_.api, eventBus_, config_.gateway.messageQueueTimeoutMs);

  browser_ = std::make_unique<browser::BrowserRelay>(config_.browser);

  tasks_ = std::make_unique<orchestration::TaskQueue>(
      config_.stateDir,
      orchestration::TaskConfig{config_.taskLane.enabled, config_.taskLane.maxQueue,
                                config_.taskLane.defaultTimeoutMs},
      eventBus_, [this](const orchestration::TaskRecord& task) {
        const std::string sessionKey = agent_->deriveSessionKey(task.channel, task.peerId);
        return agent_->routeInboundMessage(task.channel, task.peerId, task.text, task.systemEvent);
      });
  if (!tasks_->init()) {
    core::Logger::error("Cannot initialize task queue");
    return 1;
  }
  if (config_.taskLane.enabled) {
    tasks_->start();
  }

  cron_ = std::make_unique<automation::CronScheduler>(
      config_.stateDir, config_.cron.tickMs,
      [this](const automation::CronJob& job) {
        const std::string sessionKey = job.sessionKey.empty() ? ("cron:" + job.id) : job.sessionKey;
        const std::string text = "[cron " + job.name + "] " + job.message;
        const auto reply = agent_->handleMessage(sessionKey, text, true);
        core::Logger::info("Cron job fired: " + job.id + " -> " + reply);
      },
      eventBus_);

  if (!cron_->init()) {
    core::Logger::error("Cannot initialize cron scheduler");
    return 1;
  }

  if (config_.cron.enabled) {
    cron_->start();
  }

  if (!pairing_.init()) {
    core::Logger::error("Cannot initialize telegram pairing store");
    return 1;
  }

  telegram_ = std::make_unique<channels::TelegramBot>(config_.telegram, *agent_, pairing_, eventBus_);
  if (!telegram_->start()) {
    core::Logger::warn("Telegram channel failed to start (continuing)");
  }

  http_ = std::make_unique<http::HttpServer>(
      config_.http.host, config_.http.port, *agent_, sessions_, tools_, *browser_, *tasks_, *cron_,
      eventBus_, config_.api, config_.gateway.auth, config_.rateLimit, config_.audit,
      util::TimeUtil::nowMillis());

  eventBus_.publish("startup", {{"service", "clawforge"}, {"http", config_.http.host + ":" + std::to_string(config_.http.port)}, {"lang", uiLang_}});

  const bool ok = http_->start();
  if (!ok) {
    eventBus_.publish("error", {{"where", "Application::run"}, {"error", "HTTP server failed"}});
    core::Logger::error("HTTP server failed");
  }

  stop();
  return ok ? 0 : 1;
}

void Application::stop() {
  if (http_) {
    http_->stop();
  }

  if (telegram_) {
    telegram_->stop();
  }

  if (cron_) {
    cron_->stop();
  }

  if (tasks_) {
    tasks_->stop();
  }
}

}  // namespace clawforge::app
