#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "agent/AgentEngine.hpp"
#include "channels/TelegramPairingStore.hpp"
#include "core/Config.hpp"
#include "core/EventBus.hpp"

namespace clawforge::channels {

class TelegramBot {
 public:
  TelegramBot(core::TelegramConfig config, agent::AgentEngine& agent, TelegramPairingStore& pairingStore,
              core::EventBus& eventBus);
  ~TelegramBot();

  bool start();
  void stop();

 private:
  core::TelegramConfig config_;
  agent::AgentEngine& agent_;
  TelegramPairingStore& pairingStore_;
  core::EventBus& eventBus_;

  std::string botToken_;
  std::string apiBase_;

  std::atomic<bool> running_{false};
  std::thread worker_;
  int64_t offset_{0};

  void loop();
  bool allowedUser(long long userId, long long chatId, const std::string& username,
                   const std::string& firstName, std::string& denyReason) const;
  void sendMessage(long long chatId, const std::string& text) const;
};

}  // namespace clawforge::channels
