#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "core/Config.hpp"
#include "core/EventBus.hpp"
#include "llm/LlmClient.hpp"
#include "session/SessionStore.hpp"
#include "tools/ToolRegistry.hpp"

namespace clawforge::agent {

class AgentEngine {
 public:
  AgentEngine(session::SessionStore& sessions, tools::ToolRegistry& tools, llm::LlmClient& llm,
              core::ModelConfig modelConfig, core::ApiConfig apiConfig, core::EventBus& eventBus,
              int messageQueueTimeoutMs = 15000);

  std::string handleMessage(const std::string& sessionKey, const std::string& text,
                            bool systemEvent = false,
                            const nlohmann::json& runtimePolicy = nlohmann::json::object());
  std::string routeInboundMessage(const std::string& channel, const std::string& peerId,
                                  const std::string& text, bool systemEvent = false,
                                  const nlohmann::json& runtimePolicy = nlohmann::json::object());
  std::string deriveSessionKey(const std::string& channel, const std::string& peerId) const;

 private:
  session::SessionStore& sessions_;
  tools::ToolRegistry& tools_;
  llm::LlmClient& llm_;
  core::ModelConfig modelConfig_;
  core::ApiConfig apiConfig_;
  core::EventBus& eventBus_;
  std::chrono::milliseconds messageQueueTimeout_{15000};

  mutable std::mutex sessionMutexMapGuard_;
  std::unordered_map<std::string, std::shared_ptr<std::timed_mutex>> sessionMutexes_;

  std::shared_ptr<std::timed_mutex> sessionMutex(const std::string& sessionKey);
  std::string handleCommand(const std::string& sessionKey, const std::string& text,
                            const nlohmann::json& runtimePolicy);
  tools::ToolCallContext contextFromSessionKey(const std::string& sessionKey) const;
};

}  // namespace clawforge::agent
