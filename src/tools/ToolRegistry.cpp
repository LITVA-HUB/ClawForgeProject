#include "tools/ToolRegistry.hpp"

#include <algorithm>

namespace clawforge::tools {

void ToolRegistry::registerTool(const std::string& name, ToolHandler handler) {
  handlers_[name] = std::move(handler);
}

ToolRegistry::ScopePolicy ToolRegistry::buildScope(const core::ToolScopePolicyConfig& cfg) {
  return buildScope(cfg.allow, cfg.deny);
}

ToolRegistry::ScopePolicy ToolRegistry::buildScope(const std::vector<std::string>& allow,
                                                   const std::vector<std::string>& deny) {
  ScopePolicy scope;
  for (const auto& t : allow) scope.allow.insert(t);
  for (const auto& t : deny) scope.deny.insert(t);
  scope.allowConfigured = !scope.allow.empty();
  return scope;
}

void ToolRegistry::setPolicy(const core::ToolsPolicyConfig& policyConfig) {
  // stage6 source-aware schema
  globalPolicy_ = buildScope(policyConfig.global);
  channelPolicies_.clear();
  peerPolicies_.clear();

  for (const auto& [channel, scopeCfg] : policyConfig.channels) {
    channelPolicies_[channel] = buildScope(scopeCfg);
  }
  for (const auto& [peerScope, scopeCfg] : policyConfig.peers) {
    peerPolicies_[peerScope] = buildScope(scopeCfg);
  }

  // backward compatibility: legacy toolsPolicy.allow/deny
  if (!policyConfig.allow.empty() || !policyConfig.deny.empty()) {
    globalPolicy_ = buildScope(policyConfig.allow, policyConfig.deny);
  }
}

std::string ToolRegistry::peerScopeKey(const ToolCallContext& context) const {
  if (context.channel.empty() || context.peerId.empty() || context.channel == "unknown") {
    return "";
  }
  return context.channel + ":" + context.peerId;
}

bool ToolRegistry::evaluatePolicy(const std::string& name, const ToolCallContext& context,
                                  std::string* denyReason) const {
  const ScopePolicy* channelScope = nullptr;
  const ScopePolicy* peerScope = nullptr;

  const auto chIt = channelPolicies_.find(context.channel);
  if (chIt != channelPolicies_.end()) {
    channelScope = &chIt->second;
  }

  const auto peerKey = peerScopeKey(context);
  if (!peerKey.empty()) {
    const auto pIt = peerPolicies_.find(peerKey);
    if (pIt != peerPolicies_.end()) {
      peerScope = &pIt->second;
    }
  }

  if (globalPolicy_.deny.contains(name)) {
    if (denyReason) *denyReason = "denied by toolsPolicy.scopes.global.deny";
    return false;
  }
  if (channelScope && channelScope->deny.contains(name)) {
    if (denyReason) *denyReason = "denied by toolsPolicy.scopes.channels." + context.channel + ".deny";
    return false;
  }
  if (peerScope && peerScope->deny.contains(name)) {
    if (denyReason) *denyReason = "denied by toolsPolicy.scopes.peers['" + peerKey + "'].deny";
    return false;
  }

  const ScopePolicy* activeAllowScope = nullptr;
  std::string allowScopeName = "";
  if (globalPolicy_.allowConfigured) {
    activeAllowScope = &globalPolicy_;
    allowScopeName = "toolsPolicy.scopes.global.allow";
  }
  if (channelScope && channelScope->allowConfigured) {
    activeAllowScope = channelScope;
    allowScopeName = "toolsPolicy.scopes.channels." + context.channel + ".allow";
  }
  if (peerScope && peerScope->allowConfigured) {
    activeAllowScope = peerScope;
    allowScopeName = "toolsPolicy.scopes.peers['" + peerKey + "'].allow";
  }

  if (activeAllowScope && !activeAllowScope->allow.contains(name)) {
    if (denyReason) *denyReason = "not allowed by " + allowScopeName;
    return false;
  }

  return true;
}

bool ToolRegistry::isAllowed(const std::string& name, const ToolCallContext& context) const {
  return evaluatePolicy(name, context, nullptr);
}

nlohmann::json ToolRegistry::call(const std::string& name, const nlohmann::json& args,
                                  ToolCallContext context) const {
  const auto it = handlers_.find(name);
  if (it == handlers_.end()) {
    return {
        {"ok", false},
        {"error", "Unknown tool: " + name},
    };
  }

  std::string denyReason;
  if (!evaluatePolicy(name, context, &denyReason)) {
    return {
        {"ok", false},
        {"error", "Tool denied by access policy: " + name},
        {"policyReason", denyReason},
        {"context", {{"source", context.source}, {"channel", context.channel}, {"peerId", context.peerId}}},
    };
  }

  try {
    return it->second(args);
  } catch (const std::exception& e) {
    return {
        {"ok", false},
        {"error", std::string("Tool exception: ") + e.what()},
    };
  }
}

std::vector<std::string> ToolRegistry::list() const {
  std::vector<std::string> names;
  names.reserve(handlers_.size());
  for (const auto& [name, _] : handlers_) {
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::vector<std::string> ToolRegistry::allowedTools() const {
  std::vector<std::string> names;
  for (const auto& [name, _] : handlers_) {
    if (isAllowed(name, ToolCallContext{})) {
      names.push_back(name);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace clawforge::tools
