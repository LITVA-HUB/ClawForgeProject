#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Config.hpp"

namespace clawforge::llm {

struct ChatMessage {
  std::string role;
  std::string content;
};

class LlmClient {
 public:
  virtual ~LlmClient() = default;
  virtual std::string complete(const std::vector<ChatMessage>& messages) = 0;
};

class OpenAICompatClient : public LlmClient {
 public:
  explicit OpenAICompatClient(core::AppConfig config);
  std::string complete(const std::vector<ChatMessage>& messages) override;

 private:
  core::AppConfig config_;
};

std::unique_ptr<LlmClient> makeLlmClient(const core::AppConfig& config);

}  // namespace clawforge::llm
