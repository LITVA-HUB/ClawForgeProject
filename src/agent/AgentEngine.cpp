#include "agent/AgentEngine.hpp"

#include <algorithm>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/Logger.hpp"

namespace clawforge::agent {

AgentEngine::AgentEngine(session::SessionStore& sessions, tools::ToolRegistry& tools,
                         llm::LlmClient& llm, core::ModelConfig modelConfig,
                         core::ApiConfig apiConfig, core::EventBus& eventBus,
                         int messageQueueTimeoutMs)
    : sessions_(sessions),
      tools_(tools),
      llm_(llm),
      modelConfig_(std::move(modelConfig)),
      apiConfig_(std::move(apiConfig)),
      eventBus_(eventBus),
      messageQueueTimeout_(std::max(1000, messageQueueTimeoutMs)) {}

std::shared_ptr<std::timed_mutex> AgentEngine::sessionMutex(const std::string& sessionKey) {
  std::lock_guard<std::mutex> lock(sessionMutexMapGuard_);
  auto it = sessionMutexes_.find(sessionKey);
  if (it != sessionMutexes_.end()) {
    return it->second;
  }
  auto created = std::make_shared<std::timed_mutex>();
  sessionMutexes_[sessionKey] = created;
  return created;
}

std::string AgentEngine::deriveSessionKey(const std::string& channel, const std::string& peerId) const {
  const std::string normalizedChannel = channel.empty() ? "unknown-channel" : channel;
  const std::string normalizedPeer = peerId.empty() ? "unknown-peer" : peerId;

  if (apiConfig_.dmScope == "main") {
    return normalizedChannel + ":main";
  }
  if (apiConfig_.dmScope == "per-peer") {
    return normalizedChannel + ":peer:" + normalizedPeer;
  }
  if (apiConfig_.dmScope == "per-channel-peer") {
    return "channel:" + normalizedChannel + ":peer:" + normalizedPeer;
  }
  return normalizedChannel + ":main";
}

tools::ToolCallContext AgentEngine::contextFromSessionKey(const std::string& sessionKey) const {
  tools::ToolCallContext ctx;
  ctx.source = "agent-command";

  if (sessionKey.rfind("channel:", 0) == 0) {
    const auto rest = sessionKey.substr(std::string("channel:").size());
    const auto pos = rest.find(":peer:");
    if (pos != std::string::npos) {
      ctx.channel = rest.substr(0, pos);
      ctx.peerId = rest.substr(pos + std::string(":peer:").size());
      return ctx;
    }
  }

  const auto peerPos = sessionKey.find(":peer:");
  if (peerPos != std::string::npos) {
    ctx.channel = sessionKey.substr(0, peerPos);
    ctx.peerId = sessionKey.substr(peerPos + std::string(":peer:").size());
    return ctx;
  }

  const auto mainPos = sessionKey.rfind(":main");
  if (mainPos != std::string::npos && mainPos == sessionKey.size() - 5) {
    ctx.channel = sessionKey.substr(0, mainPos);
    return ctx;
  }

  ctx.channel = "unknown";
  return ctx;
}

std::string AgentEngine::routeInboundMessage(const std::string& channel, const std::string& peerId,
                                             const std::string& text, bool systemEvent) {
  const std::string sessionKey = deriveSessionKey(channel, peerId);
  eventBus_.publish("inbound_message",
                    {{"channel", channel}, {"peerId", peerId}, {"sessionKey", sessionKey}, {"text", text}});
  return handleMessage(sessionKey, text, systemEvent);
}

std::string AgentEngine::handleMessage(const std::string& sessionKey, const std::string& text,
                                       bool systemEvent) {
  if (sessionKey.empty()) {
    return "sessionKey is required";
  }

  auto lockable = sessionMutex(sessionKey);
  std::unique_lock<std::timed_mutex> lock(*lockable, std::defer_lock);
  if (!lock.try_lock_for(messageQueueTimeout_)) {
    const std::string busy = "Session is busy: another message is being processed for sessionKey='" +
                             sessionKey + "'. Try again in a moment.";
    eventBus_.publish("error", {{"where", "AgentEngine"}, {"sessionKey", sessionKey}, {"error", busy}});
    return busy;
  }

  sessions_.ensureSession(sessionKey);
  sessions_.appendMessage(sessionKey, systemEvent ? "system" : "user", text);

  if (!systemEvent && !text.empty() && text[0] == '/') {
    const auto commandReply = handleCommand(sessionKey, text);
    sessions_.appendMessage(sessionKey, "assistant", commandReply);
    eventBus_.publish("assistant_reply", {{"sessionKey", sessionKey}, {"reply", commandReply}, {"source", "command"}});
    return commandReply;
  }

  std::vector<llm::ChatMessage> prompt;
  prompt.push_back({"system", modelConfig_.systemPrompt});

  const auto history = sessions_.loadMessages(sessionKey, 30);
  for (const auto& m : history) {
    if (m.role == "system" || m.role == "user" || m.role == "assistant") {
      prompt.push_back({m.role, m.content});
    }
  }

  try {
    const std::string reply = llm_.complete(prompt);
    sessions_.appendMessage(sessionKey, "assistant", reply);
    eventBus_.publish("assistant_reply", {{"sessionKey", sessionKey}, {"reply", reply}, {"source", "llm"}});
    return reply;
  } catch (const std::exception& e) {
    eventBus_.publish("error", {{"where", "AgentEngine"}, {"sessionKey", sessionKey}, {"error", e.what()}});
    throw;
  }
}

std::string AgentEngine::handleCommand(const std::string& sessionKey, const std::string& text) {
  if (text == "/status") {
    const auto sessions = sessions_.listSessions();
    return "ClawForge OK. Sessions: " + std::to_string(sessions.size());
  }

  if (text.rfind("/tool ", 0) == 0) {
    std::istringstream ss(text.substr(6));
    std::string toolName;
    ss >> toolName;

    std::string jsonArgs;
    std::getline(ss, jsonArgs);
    if (!jsonArgs.empty() && jsonArgs[0] == ' ') {
      jsonArgs.erase(0, 1);
    }

    nlohmann::json args = nlohmann::json::object();
    if (!jsonArgs.empty()) {
      args = nlohmann::json::parse(jsonArgs, nullptr, false);
      if (args.is_discarded()) {
        return "Invalid JSON args for /tool";
      }
    }

    const auto context = contextFromSessionKey(sessionKey);
    const auto result = tools_.call(toolName, args, context);
    eventBus_.publish("tool_call_result", {{"tool", toolName}, {"ok", result.value("ok", false)}, {"result", result}});
    return result.dump(2);
  }

  if (text == "/tools") {
    const auto names = tools_.list();
    std::string out = "Available tools:";
    for (const auto& name : names) {
      out += "\n- " + name;
    }
    return out;
  }

  return "Unknown command. Use /status, /tools, or /tool <name> <json>.";
}

}  // namespace clawforge::agent
