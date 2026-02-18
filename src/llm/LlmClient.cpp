#include "llm/LlmClient.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "core/Logger.hpp"
#include "util/Shell.hpp"

namespace clawforge::llm {

using json = nlohmann::json;

namespace {

std::filesystem::path writeTempJson(const json& payload) {
  char tmpl[] = "/tmp/clawforge-payload-XXXXXX.json";
  const int fd = mkstemps(tmpl, 5);
  if (fd < 0) throw std::runtime_error("Cannot create temp payload file");
  close(fd);
  const std::filesystem::path path = tmpl;
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  out << payload.dump();
  return path;
}

std::string resolveAlias(const core::AppConfig& cfg, const std::string& ref) {
  const auto it = cfg.modelRouting.aliases.find(ref);
  if (it != cfg.modelRouting.aliases.end()) return it->second;
  return ref;
}

struct ResolvedModel {
  std::string ref;
  std::string provider;
  std::string model;
  std::string endpoint;
  std::string apiKeyEnv;
  std::string apiStyle;
};

std::optional<ResolvedModel> resolveModel(const core::AppConfig& cfg, const std::string& rawRef, std::string& err) {
  const std::string ref = resolveAlias(cfg, rawRef);
  auto modelIt = cfg.models.find(ref);
  core::ModelEntryConfig entry;
  if (modelIt != cfg.models.end()) {
    entry = modelIt->second;
  } else {
    const auto slash = ref.find('/');
    if (slash == std::string::npos) {
      err = "Unknown model/alias: " + rawRef + ". Use `models list` or `models aliases list`.";
      return std::nullopt;
    }
    entry.provider = ref.substr(0, slash);
    entry.model = ref.substr(slash + 1);
  }

  const auto pIt = cfg.modelProviders.find(entry.provider);
  if (pIt == cfg.modelProviders.end()) {
    err = "Unknown provider: " + entry.provider + ". Configure modelsConfig.providers.";
    return std::nullopt;
  }

  ResolvedModel out;
  out.ref = ref;
  out.provider = entry.provider;
  out.model = entry.model;
  out.endpoint = entry.endpoint.empty() ? pIt->second.endpoint : entry.endpoint;
  out.apiKeyEnv = entry.apiKeyEnv.empty() ? pIt->second.apiKeyEnv : entry.apiKeyEnv;
  out.apiStyle = entry.apiStyle.empty() ? pIt->second.apiStyle : entry.apiStyle;
  return out;
}

std::string parseResponseByStyle(const json& parsed, const std::string& style) {
  try {
    if (style == "openai_chat") {
      return parsed.at("choices").at(0).at("message").at("content").get<std::string>();
    }
    if (style == "anthropic_messages") {
      return parsed.at("content").at(0).at("text").get<std::string>();
    }
    if (style == "gemini_generate_content") {
      return parsed.at("candidates").at(0).at("content").at("parts").at(0).at("text").get<std::string>();
    }
  } catch (...) {
  }
  return "LLM malformed response";
}

std::string requestSingle(const ResolvedModel& model, const core::ModelConfig& baseConfig,
                          const std::vector<ChatMessage>& messages) {
  const char* key = std::getenv(model.apiKeyEnv.c_str());
  if (!key || std::string(key).empty()) {
    return "LLM key not found for " + model.ref + ": env " + model.apiKeyEnv;
  }

  json payload;
  std::string endpoint = model.endpoint;
  std::string authHeader = std::string("Authorization: Bearer ") + key;
  std::string extraHeader;

  if (model.apiStyle == "openai_chat") {
    payload["model"] = model.model;
    payload["temperature"] = baseConfig.temperature;
    payload["max_tokens"] = baseConfig.maxTokens;
    payload["messages"] = json::array();
    for (const auto& m : messages) payload["messages"].push_back({{"role", m.role}, {"content", m.content}});
  } else if (model.apiStyle == "anthropic_messages") {
    payload["model"] = model.model;
    payload["temperature"] = baseConfig.temperature;
    payload["max_tokens"] = baseConfig.maxTokens;
    payload["messages"] = json::array();
    for (const auto& m : messages) {
      if (m.role == "system") continue;
      payload["messages"].push_back({{"role", m.role == "assistant" ? "assistant" : "user"}, {"content", m.content}});
    }
    extraHeader = "anthropic-version: 2023-06-01";
    authHeader = std::string("x-api-key: ") + key;
  } else if (model.apiStyle == "gemini_generate_content") {
    endpoint = endpoint + "/" + model.model + ":generateContent?key=" + key;
    payload["contents"] = json::array();
    for (const auto& m : messages) {
      payload["contents"].push_back({{"role", m.role == "assistant" ? "model" : "user"},
                                      {"parts", json::array({{{"text", m.content}}})}});
    }
    authHeader.clear();
  } else {
    return "Unsupported provider API style '" + model.apiStyle + "' for " + model.ref +
           ". Configure modelsConfig.providers.<provider>.apiStyle or use compatible model.";
  }

  const auto tempFile = writeTempJson(payload);
  std::string cmd = "curl -sS -X POST " + util::Shell::quote(endpoint) + " -H 'Content-Type: application/json'";
  if (!authHeader.empty()) cmd += " -H " + util::Shell::quote(authHeader);
  if (!extraHeader.empty()) cmd += " -H " + util::Shell::quote(extraHeader);
  cmd += " --data @" + util::Shell::quote(tempFile.string());

  const auto res = util::Shell::run(cmd);
  std::error_code ec;
  std::filesystem::remove(tempFile, ec);

  if (res.exitCode != 0) {
    return "LLM request failed for " + model.ref;
  }

  auto parsed = json::parse(res.output, nullptr, false);
  if (parsed.is_discarded()) return "LLM parse error for " + model.ref;
  if (parsed.contains("error")) return "LLM provider error (" + model.ref + "): " + parsed["error"].dump();

  return parseResponseByStyle(parsed, model.apiStyle);
}

}  // namespace

OpenAICompatClient::OpenAICompatClient(core::AppConfig config) : config_(std::move(config)) {}

std::string OpenAICompatClient::complete(const std::vector<ChatMessage>& messages) {
  if (!config_.model.enabled) return "LLM disabled in config";

  std::vector<std::string> chain;
  chain.push_back(config_.modelRouting.current.empty() ? config_.model.model : config_.modelRouting.current);
  for (const auto& fb : config_.modelRouting.fallbacks) chain.push_back(fb);

  std::set<std::string> seen;
  std::vector<std::string> errors;
  for (const auto& ref : chain) {
    if (!seen.insert(ref).second) continue;
    std::string err;
    const auto resolved = resolveModel(config_, ref, err);
    if (!resolved.has_value()) {
      errors.push_back(err);
      continue;
    }

    const std::string out = requestSingle(*resolved, config_.model, messages);
    if (out.rfind("LLM ", 0) != 0 && out.rfind("Unsupported", 0) != 0) return out;
    errors.push_back(out);
  }

  std::string joined = "All model candidates failed";
  for (const auto& e : errors) joined += "\n- " + e;
  return joined;
}

std::unique_ptr<LlmClient> makeLlmClient(const core::AppConfig& config) {
  return std::make_unique<OpenAICompatClient>(config);
}

}  // namespace clawforge::llm
