#include "channels/TelegramBot.hpp"

#include <chrono>
#include <cstdlib>

#include <nlohmann/json.hpp>

#include "core/Logger.hpp"
#include "util/Shell.hpp"

namespace clawforge::channels {

using json = nlohmann::json;

TelegramBot::TelegramBot(core::TelegramConfig config, agent::AgentEngine& agent,
                         TelegramPairingStore& pairingStore, core::EventBus& eventBus)
    : config_(std::move(config)), agent_(agent), pairingStore_(pairingStore), eventBus_(eventBus) {}

TelegramBot::~TelegramBot() { stop(); }

bool TelegramBot::start() {
  if (!config_.enabled) {
    return true;
  }

  const char* token = std::getenv(config_.botTokenEnv.c_str());
  if (!token || std::string(token).empty()) {
    const std::string err = "Telegram enabled but token env is missing: " + config_.botTokenEnv;
    eventBus_.publish("error", {{"where", "TelegramBot::start"}, {"error", err}});
    core::Logger::warn(err);
    return false;
  }

  botToken_ = token;
  apiBase_ = "https://api.telegram.org/bot" + botToken_ + "/";

  if (running_.exchange(true)) {
    return true;
  }

  worker_ = std::thread(&TelegramBot::loop, this);
  core::Logger::info("Telegram polling started");
  return true;
}

void TelegramBot::stop() {
  if (!running_.exchange(false)) return;
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool TelegramBot::allowedUser(long long userId, long long chatId, const std::string& username,
                              const std::string& firstName, std::string& denyReason) const {
  if (config_.dmPolicy == "disabled") {
    denyReason = "Telegram access is disabled by policy.";
    return false;
  }

  if (config_.dmPolicy == "open") {
    return true;
  }

  if (config_.dmPolicy == "allowlist") {
    if (config_.allowFrom.empty()) {
      denyReason = "Allowlist policy is active, but allowFrom is empty.";
      return false;
    }
    for (auto allowed : config_.allowFrom) {
      if (allowed == userId) return true;
    }
    denyReason = "Your Telegram user is not in allowlist.";
    return false;
  }

  if (config_.dmPolicy == "pairing") {
    if (pairingStore_.isApproved(userId)) {
      return true;
    }
    const auto req = pairingStore_.ensurePending(userId, chatId, username, firstName);
    denyReason = "Pairing required. Ask admin to run: ./build/clawforge pairing approve " +
                 req.code + " (code: " + req.code + ")";
    return false;
  }

  denyReason = "Unknown dmPolicy in config.telegram.dmPolicy: " + config_.dmPolicy;
  return false;
}

void TelegramBot::sendMessage(long long chatId, const std::string& text) const {
  const std::string cmd = "curl -sS -X POST " + util::Shell::quote(apiBase_ + "sendMessage") +
                          " -d chat_id=" + std::to_string(chatId) + " --data-urlencode text=" +
                          util::Shell::quote(text);
  const auto res = util::Shell::run(cmd);
  if (res.exitCode != 0) {
    core::Logger::warn("Telegram send failed: " + res.output);
  }
}

void TelegramBot::loop() {
  while (running_.load()) {
    const std::string url = apiBase_ + "getUpdates?timeout=20&offset=" + std::to_string(offset_);
    const auto res = util::Shell::run("curl -sS " + util::Shell::quote(url));
    if (res.exitCode != 0) {
      core::Logger::warn("Telegram getUpdates failed: " + res.output);
      std::this_thread::sleep_for(std::chrono::milliseconds(config_.pollIntervalMs));
      continue;
    }

    const auto parsed = json::parse(res.output, nullptr, false);
    if (parsed.is_discarded() || !parsed.value("ok", false) || !parsed.contains("result")) {
      std::this_thread::sleep_for(std::chrono::milliseconds(config_.pollIntervalMs));
      continue;
    }

    for (const auto& update : parsed["result"]) {
      const int64_t updateId = update.value("update_id", 0LL);
      if (updateId >= offset_) {
        offset_ = updateId + 1;
      }

      if (!update.contains("message")) continue;
      const auto& msg = update["message"];

      const long long chatId = msg["chat"].value("id", 0LL);
      const long long fromId = msg["from"].value("id", 0LL);
      const std::string text = msg.value("text", "");
      const std::string username = msg["from"].value("username", "");
      const std::string firstName = msg["from"].value("first_name", "");

      if (chatId == 0 || text.empty()) continue;

      std::string denyReason;
      if (!allowedUser(fromId, chatId, username, firstName, denyReason)) {
        eventBus_.publish("inbound_message", {{"channel", "telegram"}, {"peerId", std::to_string(chatId)}, {"text", text}, {"denied", true}});
        sendMessage(chatId, denyReason);
        continue;
      }

      eventBus_.publish("inbound_message", {{"channel", "telegram"}, {"peerId", std::to_string(chatId)}, {"text", text}});
      const std::string reply = agent_.routeInboundMessage("telegram", std::to_string(chatId), text);
      eventBus_.publish("assistant_reply", {{"channel", "telegram"}, {"peerId", std::to_string(chatId)}, {"reply", reply}});
      sendMessage(chatId, reply);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(config_.pollIntervalMs));
  }
}

}  // namespace clawforge::channels
