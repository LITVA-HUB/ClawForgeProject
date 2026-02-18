#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/Config.hpp"

namespace clawforge::tools {

using ToolHandler = std::function<nlohmann::json(const nlohmann::json&)>;

struct ToolCallContext {
  std::string source{"unknown"};
  std::string channel{"unknown"};
  std::string peerId;
};

class ToolRegistry {
 public:
  void registerTool(const std::string& name, ToolHandler handler);
  void setPolicy(const core::ToolsPolicyConfig& policyConfig);
  nlohmann::json call(const std::string& name, const nlohmann::json& args,
                      ToolCallContext context = {}) const;
  std::vector<std::string> list() const;
  std::vector<std::string> allowedTools() const;
  bool isAllowed(const std::string& name, const ToolCallContext& context = {}) const;

 private:
  struct ScopePolicy {
    std::unordered_set<std::string> allow;
    std::unordered_set<std::string> deny;
    bool allowConfigured{false};
  };

  std::unordered_map<std::string, ToolHandler> handlers_;
  ScopePolicy globalPolicy_;
  std::unordered_map<std::string, ScopePolicy> channelPolicies_;
  std::unordered_map<std::string, ScopePolicy> peerPolicies_;

  static ScopePolicy buildScope(const core::ToolScopePolicyConfig& cfg);
  static ScopePolicy buildScope(const std::vector<std::string>& allow,
                                const std::vector<std::string>& deny);

  bool evaluatePolicy(const std::string& name, const ToolCallContext& context,
                      std::string* denyReason = nullptr) const;
  std::string peerScopeKey(const ToolCallContext& context) const;
};

}  // namespace clawforge::tools
