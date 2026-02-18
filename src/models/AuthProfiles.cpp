#include "models/AuthProfiles.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace clawforge::models {

namespace {
using json = nlohmann::json;

std::string fmtUtc(std::chrono::system_clock::time_point tp) {
  const std::time_t t = std::chrono::system_clock::to_time_t(tp);
  std::tm gmt{};
#if defined(_WIN32)
  gmtime_s(&gmt, &t);
#else
  gmt = *std::gmtime(&t);
#endif
  std::ostringstream oss;
  oss << std::put_time(&gmt, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::optional<std::chrono::system_clock::time_point> parseUtc(const std::string& s) {
  if (s.empty()) return std::nullopt;
  std::tm tm{};
  std::istringstream iss(s);
  iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  if (iss.fail()) return std::nullopt;
#if defined(_WIN32)
  const std::time_t t = _mkgmtime(&tm);
#else
  const std::time_t t = timegm(&tm);
#endif
  if (t < 0) return std::nullopt;
  return std::chrono::system_clock::from_time_t(t);
}

AuthProfile profileFromJson(const json& j) {
  AuthProfile p;
  p.id = j.value("id", "");
  p.provider = j.value("provider", "");
  p.mode = j.value("mode", "api_key");
  p.token = j.value("token", "");
  p.expiresAt = j.value("expiresAt", "");
  if (j.contains("meta") && j["meta"].is_object()) p.meta = j["meta"];
  return p;
}

json profileToJson(const AuthProfile& p) {
  return json{{"id", p.id},
              {"provider", p.provider},
              {"mode", p.mode},
              {"token", p.token},
              {"expiresAt", p.expiresAt},
              {"meta", p.meta.is_object() ? p.meta : json::object()}};
}

}  // namespace

AuthProfileStore::AuthProfileStore(std::filesystem::path stateDir)
    : path_(std::move(stateDir) / "models" / "auth-profiles.json") {}

bool AuthProfileStore::init() {
  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);
  if (!std::filesystem::exists(path_)) {
    root_ = json{{"profiles", json::array()}, {"active", json::object()}, {"updatedAt", nowUtcRfc3339()}};
    return save();
  }
  return load();
}

bool AuthProfileStore::load() {
  std::ifstream in(path_);
  if (!in) return false;
  auto j = json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  root_ = std::move(j);
  if (!root_.contains("profiles") || !root_["profiles"].is_array()) root_["profiles"] = json::array();
  if (!root_.contains("active") || !root_["active"].is_object()) root_["active"] = json::object();
  return true;
}

bool AuthProfileStore::save() const {
  std::ofstream out(path_, std::ios::trunc);
  if (!out) return false;
  out << root_.dump(2) << "\n";
  return true;
}

std::vector<AuthProfile> AuthProfileStore::list() const {
  std::vector<AuthProfile> out;
  if (!root_.contains("profiles") || !root_["profiles"].is_array()) return out;
  for (const auto& it : root_["profiles"]) {
    if (!it.is_object()) continue;
    out.push_back(profileFromJson(it));
  }
  return out;
}

std::optional<AuthProfile> AuthProfileStore::getById(const std::string& id) const {
  for (const auto& p : list()) if (p.id == id) return p;
  return std::nullopt;
}

std::optional<std::string> AuthProfileStore::activeProfileId(const std::string& provider) const {
  if (!root_.contains("active") || !root_["active"].is_object()) return std::nullopt;
  if (!root_["active"].contains(provider)) return std::nullopt;
  const auto& v = root_["active"][provider];
  if (!v.is_string()) return std::nullopt;
  return v.get<std::string>();
}

std::optional<AuthProfile> AuthProfileStore::getActiveForProvider(const std::string& provider) const {
  const auto activeId = activeProfileId(provider);
  if (!activeId.has_value()) return std::nullopt;
  return getById(*activeId);
}

bool AuthProfileStore::upsert(const AuthProfile& profile) {
  if (!root_.contains("profiles") || !root_["profiles"].is_array()) root_["profiles"] = json::array();
  bool replaced = false;
  for (auto& it : root_["profiles"]) {
    if (!it.is_object()) continue;
    if (it.value("id", "") == profile.id) {
      it = profileToJson(profile);
      replaced = true;
      break;
    }
  }
  if (!replaced) root_["profiles"].push_back(profileToJson(profile));
  root_["updatedAt"] = nowUtcRfc3339();
  return save();
}

bool AuthProfileStore::remove(const std::string& profileId) {
  if (!root_.contains("profiles") || !root_["profiles"].is_array()) return false;
  json out = json::array();
  bool removed = false;
  for (const auto& it : root_["profiles"]) {
    if (!it.is_object()) continue;
    if (it.value("id", "") == profileId) {
      removed = true;
      continue;
    }
    out.push_back(it);
  }
  if (!removed) return false;
  root_["profiles"] = std::move(out);
  if (root_.contains("active") && root_["active"].is_object()) {
    std::vector<std::string> eraseKeys;
    for (auto it = root_["active"].begin(); it != root_["active"].end(); ++it) {
      if (it.value().is_string() && it.value().get<std::string>() == profileId) eraseKeys.push_back(it.key());
    }
    for (const auto& k : eraseKeys) root_["active"].erase(k);
  }
  root_["updatedAt"] = nowUtcRfc3339();
  return save();
}

bool AuthProfileStore::setActive(const std::string& provider, const std::string& profileId) {
  const auto p = getById(profileId);
  if (!p.has_value() || p->provider != provider) return false;
  if (!root_.contains("active") || !root_["active"].is_object()) root_["active"] = json::object();
  root_["active"][provider] = profileId;
  root_["updatedAt"] = nowUtcRfc3339();
  return save();
}

ResolvedAuth AuthProfileStore::resolveForProvider(const std::filesystem::path& stateDir,
                                                  const std::string& provider,
                                                  const std::string& envKeyName) {
  ResolvedAuth out;
  out.source = "missing";

  AuthProfileStore store(stateDir);
  if (store.init()) {
    const auto profile = store.getActiveForProvider(provider);
    if (profile.has_value() && !profile->token.empty()) {
      out.token = profile->token;
      out.source = "profile";
      out.mode = profile->mode;
      out.profileId = profile->id;
      out.expiresAt = profile->expiresAt;
      out.expired = isExpired(profile->expiresAt);
      if (out.expired) out.warnings.push_back("active auth profile token is expired");
      if (!out.expired) return out;
    }
  }

  const char* env = std::getenv(envKeyName.c_str());
  if (env && std::string(env).size() > 0) {
    out.token = env;
    out.source = "env";
    out.mode = "env";
    out.profileId.clear();
    if (out.expired) out.warnings.push_back("falling back to env key because profile token is expired");
  }
  return out;
}

std::string AuthProfileStore::nowUtcRfc3339() { return fmtUtc(std::chrono::system_clock::now()); }

std::string AuthProfileStore::addSecondsUtcRfc3339(int seconds) {
  return fmtUtc(std::chrono::system_clock::now() + std::chrono::seconds(seconds));
}

bool AuthProfileStore::isExpired(const std::string& expiresAt) {
  const auto tp = parseUtc(expiresAt);
  if (!tp.has_value()) return false;
  return std::chrono::system_clock::now() >= *tp;
}

}  // namespace clawforge::models
