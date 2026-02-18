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

  cfg.modelProviders = {
      {"openai", {"https://api.openai.com/v1/chat/completions", "OPENAI_API_KEY", "openai_chat"}},
      {"anthropic", {"https://api.anthropic.com/v1/messages", "ANTHROPIC_API_KEY", "anthropic_messages"}},
      {"openrouter", {"https://openrouter.ai/api/v1/chat/completions", "OPENROUTER_API_KEY", "openai_chat"}},
      {"gemini", {"https://generativelanguage.googleapis.com/v1beta/models", "GEMINI_API_KEY", "gemini_generate_content"}},
      {"minimax", {"https://api.minimax.io/v1/text/chatcompletion_v2", "MINIMAX_API_KEY", "openai_chat"}},
  };
  cfg.models = {
      {"openai/gpt-4o-mini", {"openai", "gpt-4o-mini", "", "", ""}},
      {"anthropic/claude-3-5-haiku-latest", {"anthropic", "claude-3-5-haiku-latest", "", "", ""}},
      {"openrouter/openai/gpt-4o-mini", {"openrouter", "openai/gpt-4o-mini", "", "", ""}},
      {"gemini/gemini-1.5-flash", {"gemini", "gemini-1.5-flash", "", "", ""}},
      {"minimax/abab6.5-chat", {"minimax", "abab6.5-chat", "", "", ""}},
  };
  cfg.modelRouting.aliases = {{"default", "openai/gpt-4o-mini"}, {"fast", "openai/gpt-4o-mini"}};

  if (j.contains("modelsConfig") && j["modelsConfig"].is_object()) {
    const auto& mc = j["modelsConfig"];
    if (mc.contains("providers") && mc["providers"].is_object()) {
      cfg.modelProviders.clear();
      for (auto it = mc["providers"].begin(); it != mc["providers"].end(); ++it) {
        if (!it.value().is_object()) continue;
        ModelProviderConfig p;
        p.endpoint = it.value().value("endpoint", "");
        p.apiKeyEnv = it.value().value("apiKeyEnv", "");
        p.apiStyle = it.value().value("apiStyle", p.apiStyle);
        cfg.modelProviders[it.key()] = std::move(p);
      }
    }
    if (mc.contains("models") && mc["models"].is_object()) {
      cfg.models.clear();
      for (auto it = mc["models"].begin(); it != mc["models"].end(); ++it) {
        if (!it.value().is_object()) continue;
        ModelEntryConfig m;
        m.provider = it.value().value("provider", "");
        m.model = it.value().value("model", "");
        m.endpoint = it.value().value("endpoint", "");
        m.apiKeyEnv = it.value().value("apiKeyEnv", "");
        m.apiStyle = it.value().value("apiStyle", "");
        cfg.models[it.key()] = std::move(m);
      }
    }
    if (mc.contains("routing") && mc["routing"].is_object()) {
      const auto& r = mc["routing"];
      cfg.modelRouting.current = r.value("current", cfg.modelRouting.current);
      if (r.contains("aliases") && r["aliases"].is_object()) {
        cfg.modelRouting.aliases.clear();
        for (auto it = r["aliases"].begin(); it != r["aliases"].end(); ++it) {
          cfg.modelRouting.aliases[it.key()] = it.value().get<std::string>();
        }
      }
      if (r.contains("fallbacks") && r["fallbacks"].is_array()) {
        cfg.modelRouting.fallbacks.clear();
        for (const auto& fb : r["fallbacks"]) cfg.modelRouting.fallbacks.push_back(fb.get<std::string>());
      }
    }
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
    cfg.browser.profile = b.value("profile", cfg.browser.profile);
    cfg.browser.cliBinary = b.value("cliBinary", cfg.browser.cliBinary);
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
