#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace clawforge::models {

struct AuthProfile {
  std::string id;
  std::string provider;
  std::string mode;      // api_key | oauth_token
  std::string token;
  std::string expiresAt; // RFC3339 UTC, optional
  nlohmann::json meta = nlohmann::json::object();
};

struct ResolvedAuth {
  std::string token;
  std::string source; // profile | env | missing
  std::string mode;   // api_key | oauth_token | env
  std::string profileId;
  std::string expiresAt;
  bool expired{false};
  bool refreshed{false};
  std::vector<std::string> warnings;
};

class AuthProfileStore {
 public:
  explicit AuthProfileStore(std::filesystem::path stateDir);

  bool init();
  std::vector<AuthProfile> list() const;
  std::optional<AuthProfile> getById(const std::string& id) const;
  std::optional<AuthProfile> getActiveForProvider(const std::string& provider) const;
  bool upsert(const AuthProfile& profile);
  bool remove(const std::string& profileId);
  bool setActive(const std::string& provider, const std::string& profileId);
  std::optional<std::string> activeProfileId(const std::string& provider) const;
  std::vector<std::string> orderForProvider(const std::string& provider) const;
  bool setOrderForProvider(const std::string& provider, const std::vector<std::string>& profileIds);
  bool clearOrderForProvider(const std::string& provider);

  static ResolvedAuth resolveForProvider(const std::filesystem::path& stateDir,
                                         const std::string& provider,
                                         const std::string& envKeyName);
  static std::string nowUtcRfc3339();
  static std::string addSecondsUtcRfc3339(int seconds);
  static bool isExpired(const std::string& expiresAt);

 private:
  std::filesystem::path path_;
  nlohmann::json root_;

  bool load();
  bool save() const;
};

}  // namespace clawforge::models
