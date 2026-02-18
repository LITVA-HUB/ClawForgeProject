#include "core/Config.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace clawforge::core {

using json = nlohmann::json;

namespace {

void loadScope(const json& src, ToolScopePolicyConfig& dst) {
  if (src.contains("allow") && src["allow"].is_array()) {
    dst.allow.clear();
    for (const auto& tool : src["allow"]) {
      dst.allow.push_back(tool.get<std::string>());
    }
  }
  if (src.contains("deny") && src["deny"].is_array()) {
    dst.deny.clear();
    for (const auto& tool : src["deny"]) {
      dst.deny.push_back(tool.get<std::string>());
    }
  }
}

}  // namespace

AppConfig AppConfig::loadFromFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("Cannot open config file: " + path.string());
  }

  json j;
  in >> j;

  AppConfig cfg;

  cfg.name = j.value("name", cfg.name);
  cfg.workspace = j.value("workspace", cfg.workspace.string());
  cfg.stateDir = j.value("stateDir", cfg.stateDir.string());

  if (j.contains("http")) {
    const auto& h = j["http"];
    cfg.http.host = h.value("host", cfg.http.host);
    cfg.http.port = h.value("port", cfg.http.port);
  }

  if (j.contains("model")) {
    const auto& m = j["model"];
    cfg.model.enabled = m.value("enabled", cfg.model.enabled);
    cfg.model.endpoint = m.value("endpoint", cfg.model.endpoint);
    cfg.model.model = m.value("model", cfg.model.model);
    cfg.model.apiKeyEnv = m.value("apiKeyEnv", cfg.model.apiKeyEnv);
    cfg.model.temperature = m.value("temperature", cfg.model.temperature);
    cfg.model.maxTokens = m.value("maxTokens", cfg.model.maxTokens);
    cfg.model.systemPrompt = m.value("systemPrompt", cfg.model.systemPrompt);
  }

  if (j.contains("telegram")) {
    const auto& t = j["telegram"];
    cfg.telegram.enabled = t.value("enabled", cfg.telegram.enabled);
    cfg.telegram.botTokenEnv = t.value("botTokenEnv", cfg.telegram.botTokenEnv);
    cfg.telegram.pollIntervalMs = t.value("pollIntervalMs", cfg.telegram.pollIntervalMs);
    if (t.contains("allowFrom") && t["allowFrom"].is_array()) {
      cfg.telegram.allowFrom.clear();
      for (const auto& id : t["allowFrom"]) {
        cfg.telegram.allowFrom.push_back(id.get<long long>());
      }
    }
    cfg.telegram.dmPolicy = t.value("dmPolicy", cfg.telegram.dmPolicy);
  }

  if (j.contains("cron")) {
    const auto& c = j["cron"];
    cfg.cron.enabled = c.value("enabled", cfg.cron.enabled);
    cfg.cron.tickMs = c.value("tickMs", cfg.cron.tickMs);
  }

  if (j.contains("api")) {
    const auto& a = j["api"];
    cfg.api.dmScope = a.value("dmScope", cfg.api.dmScope);
  }

  if (j.contains("toolsPolicy")) {
    const auto& p = j["toolsPolicy"];

    // legacy fallback
    if (p.contains("allow") && p["allow"].is_array()) {
      cfg.toolsPolicy.allow.clear();
      for (const auto& tool : p["allow"]) {
        cfg.toolsPolicy.allow.push_back(tool.get<std::string>());
      }
    }
    if (p.contains("deny") && p["deny"].is_array()) {
      cfg.toolsPolicy.deny.clear();
      for (const auto& tool : p["deny"]) {
        cfg.toolsPolicy.deny.push_back(tool.get<std::string>());
      }
    }

    if (p.contains("scopes") && p["scopes"].is_object()) {
      const auto& s = p["scopes"];
      if (s.contains("global") && s["global"].is_object()) {
        loadScope(s["global"], cfg.toolsPolicy.global);
      }

      if (s.contains("channels") && s["channels"].is_object()) {
        cfg.toolsPolicy.channels.clear();
        for (auto it = s["channels"].begin(); it != s["channels"].end(); ++it) {
          if (!it.value().is_object()) continue;
          ToolScopePolicyConfig scope;
          loadScope(it.value(), scope);
          cfg.toolsPolicy.channels[it.key()] = std::move(scope);
        }
      }

      if (s.contains("peers") && s["peers"].is_object()) {
        cfg.toolsPolicy.peers.clear();
        for (auto it = s["peers"].begin(); it != s["peers"].end(); ++it) {
          if (!it.value().is_object()) continue;
          ToolScopePolicyConfig scope;
          loadScope(it.value(), scope);
          cfg.toolsPolicy.peers[it.key()] = std::move(scope);
        }
      }
    }
  }

  if (j.contains("gateway")) {
    const auto& g = j["gateway"];
    cfg.gateway.messageQueueTimeoutMs = g.value("messageQueueTimeoutMs", cfg.gateway.messageQueueTimeoutMs);
    if (g.contains("auth")) {
      const auto& a = g["auth"];
      cfg.gateway.auth.mode = a.value("mode", cfg.gateway.auth.mode);
      cfg.gateway.auth.tokenEnv = a.value("tokenEnv", cfg.gateway.auth.tokenEnv);
    }
  }

  if (j.contains("browser")) {
    const auto& b = j["browser"];
    cfg.browser.enabled = b.value("enabled", cfg.browser.enabled);
    cfg.browser.backend = b.value("backend", cfg.browser.backend);
    cfg.browser.openCommand = b.value("openCommand", cfg.browser.openCommand);
    cfg.browser.openTimeoutMs = b.value("openTimeoutMs", cfg.browser.openTimeoutMs);
  }

  if (j.contains("taskLane")) {
    const auto& t = j["taskLane"];
    cfg.taskLane.enabled = t.value("enabled", cfg.taskLane.enabled);
    cfg.taskLane.maxQueue = t.value("maxQueue", cfg.taskLane.maxQueue);
    cfg.taskLane.defaultTimeoutMs = t.value("defaultTimeoutMs", cfg.taskLane.defaultTimeoutMs);
  }

  if (j.contains("rateLimit")) {
    const auto& r = j["rateLimit"];
    cfg.rateLimit.enabled = r.value("enabled", cfg.rateLimit.enabled);
    cfg.rateLimit.maxRequests = r.value("maxRequests", cfg.rateLimit.maxRequests);
    cfg.rateLimit.windowMs = r.value("windowMs", cfg.rateLimit.windowMs);
  }

  if (j.contains("audit")) {
    const auto& a = j["audit"];
    cfg.audit.enabled = a.value("enabled", cfg.audit.enabled);
    cfg.audit.file = a.value("file", cfg.audit.file);
  }

  return cfg;
}

}  // namespace clawforge::core
