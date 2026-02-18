#pragma once

#include <memory>
#include <string>

#include "agent/AgentEngine.hpp"
#include "automation/CronScheduler.hpp"
#include "browser/BrowserRelay.hpp"
#include "channels/TelegramBot.hpp"
#include "channels/TelegramPairingStore.hpp"
#include "core/Config.hpp"
#include "core/EventBus.hpp"
#include "http/HttpServer.hpp"
#include "llm/LlmClient.hpp"
#include "orchestration/TaskQueue.hpp"
#include "session/SessionStore.hpp"
#include "tools/ToolRegistry.hpp"

namespace clawforge::app {

class Application {
 public:
  explicit Application(core::AppConfig config, std::string uiLang = "ru");
  int run();
  void stop();

 private:
  core::AppConfig config_;
  std::string uiLang_;

  core::EventBus eventBus_;
  session::SessionStore sessions_;
  channels::TelegramPairingStore pairing_;
  tools::ToolRegistry tools_;
  std::unique_ptr<llm::LlmClient> llm_;
  std::unique_ptr<agent::AgentEngine> agent_;
  std::unique_ptr<browser::BrowserRelay> browser_;
  std::unique_ptr<orchestration::TaskQueue> tasks_;
  std::unique_ptr<automation::CronScheduler> cron_;
  std::unique_ptr<http::HttpServer> http_;
  std::unique_ptr<channels::TelegramBot> telegram_;
};

}  // namespace clawforge::app
