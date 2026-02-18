#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace clawforge::core {

struct HttpConfig {
  std::string host{"127.0.0.1"};
  int port{18890};
};

struct ModelConfig {
  bool enabled{true};
  std::string endpoint{"https://api.openai.com/v1/chat/completions"};
  std::string model{"gpt-4o-mini"};
  std::string apiKeyEnv{"OPENAI_API_KEY"};
  double temperature{0.2};
  int maxTokens{700};
  std::string systemPrompt{"You are NexaClaw, a concise engineering assistant."};
};

struct TelegramConfig {
  bool enabled{false};
  std::string botTokenEnv{"TELEGRAM_BOT_TOKEN"};
  int pollIntervalMs{1500};
  std::vector<long long> allowFrom;
  std::string dmPolicy{"open"};  // open | allowlist | pairing | disabled
};

struct CronConfig {
  bool enabled{true};
  int tickMs{1000};
};

struct ApiConfig {
  std::string dmScope{"main"};  // main | per-peer | per-channel-peer
};

struct ToolScopePolicyConfig {
  std::vector<std::string> allow;
  std::vector<std::string> deny;
};

struct ToolsPolicyConfig {
  // legacy top-level compatibility: toolsPolicy.allow / toolsPolicy.deny
  std::vector<std::string> allow;
  std::vector<std::string> deny;

  // stage6 schema: toolsPolicy.scopes.global/channels/peers
  ToolScopePolicyConfig global;
  std::map<std::string, ToolScopePolicyConfig> channels;
  std::map<std::string, ToolScopePolicyConfig> peers;  // key: channel:peerId
};

struct GatewayAuthConfig {
  std::string mode{"off"};  // off | token
  std::string tokenEnv{"NEXACLAW_GATEWAY_TOKEN"};
};

struct GatewayConfig {
  GatewayAuthConfig auth;
  int messageQueueTimeoutMs{15000};
};

struct BrowserConfig {
  bool enabled{true};
  std::string backend{"stub"};     // stub | shell
  std::string openCommand{"open"}; // shell backend command prefix
  int openTimeoutMs{15000};
};

struct TaskLaneConfig {
  bool enabled{true};
  int maxQueue{256};
  int defaultTimeoutMs{30000};
};

struct RateLimitConfig {
  bool enabled{true};
  int maxRequests{120};
  int windowMs{60000};
};

struct AuditConfig {
  bool enabled{true};
  std::string file{"./state/audit/events.jsonl"};
};

struct ModelProviderConfig {
  std::string endpoint;
  std::string apiKeyEnv;
  std::string apiStyle{"openai_chat"};  // openai_chat | anthropic_messages | gemini_generate_content
};

struct ModelEntryConfig {
  std::string provider;
  std::string model;
  std::string endpoint;
  std::string apiKeyEnv;
  std::string apiStyle;
};

struct ModelRoutingConfig {
  std::string current{"openai/gpt-4o-mini"};
  std::map<std::string, std::string> aliases;
  std::vector<std::string> fallbacks;
};

struct AppConfig {
  std::string name{"nexaclaw"};
  std::filesystem::path workspace{"./workspace"};
  std::filesystem::path stateDir{"./state"};
  HttpConfig http;
  ModelConfig model;
  std::map<std::string, ModelProviderConfig> modelProviders;
  std::map<std::string, ModelEntryConfig> models;
  ModelRoutingConfig modelRouting;
  TelegramConfig telegram;
  CronConfig cron;
  ApiConfig api;
  ToolsPolicyConfig toolsPolicy;
  GatewayConfig gateway;
  BrowserConfig browser;
  TaskLaneConfig taskLane;
  RateLimitConfig rateLimit;
  AuditConfig audit;

  static AppConfig loadFromFile(const std::filesystem::path& path);
};

}  // namespace clawforge::core
