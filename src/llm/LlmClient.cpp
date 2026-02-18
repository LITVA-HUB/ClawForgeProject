#include "llm/LlmClient.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  const int fd = mkstemps(tmpl, 5);  // suffix .json
  if (fd < 0) {
    throw std::runtime_error("Cannot create temp payload file");
  }
  close(fd);

  const std::filesystem::path path = tmpl;
  std::ofstream out(path, std::ios::out | std::ios::trunc);
  out << payload.dump();
  return path;
}

}  // namespace

OpenAICompatClient::OpenAICompatClient(core::ModelConfig config) : config_(std::move(config)) {}

std::string OpenAICompatClient::complete(const std::vector<ChatMessage>& messages) {
  if (!config_.enabled) {
    return "LLM disabled in config";
  }

  const char* key = std::getenv(config_.apiKeyEnv.c_str());
  if (!key || std::string(key).empty()) {
    return "LLM key not found in env: " + config_.apiKeyEnv;
  }

  json payload;
  payload["model"] = config_.model;
  payload["temperature"] = config_.temperature;
  payload["max_tokens"] = config_.maxTokens;
  payload["messages"] = json::array();
  for (const auto& m : messages) {
    payload["messages"].push_back({{"role", m.role}, {"content", m.content}});
  }

  const auto tempFile = writeTempJson(payload);
  const std::string cmd =
      "curl -sS -X POST " + util::Shell::quote(config_.endpoint) +
      " -H 'Content-Type: application/json' -H " +
      util::Shell::quote(std::string("Authorization: Bearer ") + key) +
      " --data @" + util::Shell::quote(tempFile.string());

  const auto res = util::Shell::run(cmd);
  std::error_code ec;
  std::filesystem::remove(tempFile, ec);

  if (res.exitCode != 0) {
    core::Logger::error("LLM request failed: " + res.output);
    return "LLM request failed";
  }

  auto parsed = json::parse(res.output, nullptr, false);
  if (parsed.is_discarded()) {
    return "LLM parse error: " + res.output.substr(0, std::min<std::size_t>(res.output.size(), 200));
  }

  if (parsed.contains("error")) {
    return "LLM error: " + parsed["error"].dump();
  }

  try {
    return parsed.at("choices").at(0).at("message").at("content").get<std::string>();
  } catch (...) {
    return "LLM malformed response";
  }
}

std::unique_ptr<LlmClient> makeLlmClient(const core::ModelConfig& config) {
  return std::make_unique<OpenAICompatClient>(config);
}

}  // namespace clawforge::llm
