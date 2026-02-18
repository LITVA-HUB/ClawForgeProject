#include <algorithm>
#include <csignal>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "app/Application.hpp"
#include "automation/CronScheduler.hpp"
#include "browser/BrowserRelay.hpp"
#include "channels/TelegramPairingStore.hpp"
#include "core/Config.hpp"
#include "core/EventBus.hpp"
#include "core/Logger.hpp"
#include "models/AuthProfiles.hpp"
#include "session/SessionStore.hpp"
#include "tools/BuiltinTools.hpp"
#include "tools/ToolRegistry.hpp"
#include "util/FileUtil.hpp"
#include "util/Shell.hpp"

namespace {
using json = nlohmann::json;
clawforge::app::Application* g_app = nullptr;

std::string normalizeLang(std::string lang) { for (char& ch : lang) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); if (lang.rfind("en", 0) == 0) return "en"; if (lang.rfind("ru", 0) == 0) return "ru"; return "ru"; }
std::string detectLang() { const char* envLang = std::getenv("LANG"); return (!envLang || std::string(envLang).empty()) ? "ru" : normalizeLang(envLang); }
void onSignal(int) { if (g_app) { clawforge::core::Logger::info("Signal received, stopping..."); g_app->stop(); } }
std::string authHeaderFromEnv(const clawforge::core::AppConfig& cfg) { if (cfg.gateway.auth.mode != "token") return ""; const char* token = std::getenv(cfg.gateway.auth.tokenEnv.c_str()); if (!token || std::string(token).empty()) return ""; return "Authorization: Bearer " + std::string(token); }

std::optional<json> httpRequestJson(const std::string& method, const std::string& url, const std::string& authHeader = "", const std::optional<json>& body = std::nullopt) {
  std::string cmd = "curl -sS --max-time 3 -X " + clawforge::util::Shell::quote(method) + " " + clawforge::util::Shell::quote(url);
  if (!authHeader.empty()) cmd += " -H " + clawforge::util::Shell::quote(authHeader);
  if (body.has_value()) {
    cmd += " -H " + clawforge::util::Shell::quote("Content-Type: application/json");
    cmd += " --data " + clawforge::util::Shell::quote(body->dump());
  }
  const auto res = clawforge::util::Shell::run(cmd);
  if (res.exitCode != 0) return std::nullopt;
  auto parsed = json::parse(res.output, nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  return parsed;
}

std::optional<json> httpGetJson(const std::string& url, const std::string& authHeader = "") { return httpRequestJson("GET", url, authHeader, std::nullopt); }
std::optional<json> httpPostJson(const std::string& url, const json& body, const std::string& authHeader = "") { return httpRequestJson("POST", url, authHeader, body); }
std::optional<json> httpDeleteJson(const std::string& url, const std::string& authHeader = "") { return httpRequestJson("DELETE", url, authHeader, std::nullopt); }

json loadJsonFile(const std::string& path) { std::ifstream in(path); if (!in) throw std::runtime_error("Cannot open config file: " + path); json j; in >> j; return j; }
void saveJsonFile(const std::string& path, const json& j) { std::ofstream out(path, std::ios::trunc); if (!out) throw std::runtime_error("Cannot write config file: " + path); out << j.dump(2) << "\n"; }

std::string hashText(const std::string& text) {
  const auto h = static_cast<unsigned long long>(std::hash<std::string>{}(text));
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%016llx", h);
  return std::string(buf);
}

std::string jsonHash(const json& j) { return hashText(j.dump()); }

bool isLoopbackHost(const std::string& host) {
  return host == "127.0.0.1" || host == "localhost" || host == "::1";
}

bool hasEnvToken(const std::string& envName) {
  const char* v = std::getenv(envName.c_str());
  return v && std::string(v).size() > 0;
}

bool permsTooOpen(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) return false;
  const auto perms = std::filesystem::status(path).permissions();
  using P = std::filesystem::perms;
  const bool groupAny = (perms & (P::group_read | P::group_write | P::group_exec)) != P::none;
  const bool otherAny = (perms & (P::others_read | P::others_write | P::others_exec)) != P::none;
  return groupAny || otherAny;
}

void tightenPermsOwnerOnly(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) return;
  std::error_code ec;
  const bool isDir = std::filesystem::is_directory(path, ec);
  const auto wanted = isDir
                          ? (std::filesystem::perms::owner_all)
                          : (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  std::filesystem::permissions(path, wanted, std::filesystem::perm_options::replace, ec);
}

json mergePatch(json target, const json& patch) {
  if (!patch.is_object()) return patch;
  if (!target.is_object()) target = json::object();
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.value().is_null()) {
      target.erase(it.key());
    } else {
      target[it.key()] = mergePatch(target.contains(it.key()) ? target[it.key()] : json(), it.value());
    }
  }
  return target;
}

bool validateConfigJson(const json& candidate, std::string& error) {
  const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const auto tmp = std::filesystem::temp_directory_path() / ("nexaclaw-validate-" + std::to_string(nowNs) + ".json");
  try {
    saveJsonFile(tmp.string(), candidate);
    auto cfg = clawforge::core::AppConfig::loadFromFile(tmp);
    (void)cfg;
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    return false;
  }
}

std::filesystem::path gatewayPidFile(const clawforge::core::AppConfig& cfg) {
  return cfg.stateDir / "gateway" / "gateway.pid";
}

std::filesystem::path gatewayLogFile(const clawforge::core::AppConfig& cfg) {
  return cfg.stateDir / "logs" / "gateway.log";
}

std::optional<std::string> readPidFile(const std::filesystem::path& pidFile) {
  if (!std::filesystem::exists(pidFile)) return std::nullopt;
  std::ifstream in(pidFile);
  if (!in) return std::nullopt;
  std::string pid;
  in >> pid;
  if (pid.empty()) return std::nullopt;
  return pid;
}

bool pidRunning(const std::string& pid) {
  if (pid.empty()) return false;
  const auto probe = clawforge::util::Shell::run("kill -0 " + clawforge::util::Shell::quote(pid) + " >/dev/null 2>&1");
  return probe.exitCode == 0;
}

void printCompatNotImplemented(const std::string& cmd, const std::string& lang) {
  std::cout << (lang == "ru" ? "Команда совместимости OpenClaw пока не реализована: " : "OpenClaw compatibility command is not implemented yet: ")
            << cmd << "\n" << (lang == "ru" ? "Смотри docs/CLI_PARITY.md" : "See docs/CLI_PARITY.md") << std::endl;
}

std::optional<std::string> argValue(const std::vector<std::string>& pos, const std::string& key) {
  for (size_t i = 0; i + 1 < pos.size(); ++i) if (pos[i] == key) return pos[i + 1];
  return std::nullopt;
}

bool hasFlag(const std::vector<std::string>& pos, const std::string& flag) {
  for (const auto& x : pos) if (x == flag) return true;
  return false;
}

std::string trim(std::string value) {
  auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
  value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
  return value;
}

std::string promptLine(const std::string& label, const std::string& defaultValue = "") {
  if (defaultValue.empty()) std::cout << label << ": ";
  else std::cout << label << " [" << defaultValue << "]: ";
  std::string in;
  std::getline(std::cin, in);
  in = trim(in);
  if (in.empty()) return defaultValue;
  return in;
}

bool ensureConfigFile(const std::string& configPath, std::string& error) {
  const auto dst = std::filesystem::path(configPath);
  if (std::filesystem::exists(dst)) return true;
  const auto src = std::filesystem::path("config/config.example.json");
  if (!std::filesystem::exists(src)) {
    error = "Template not found: " + src.string();
    return false;
  }
  std::error_code ec;
  if (dst.has_parent_path()) std::filesystem::create_directories(dst.parent_path(), ec);
  if (!std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec) || ec) {
    error = "Cannot create config from template: " + dst.string();
    return false;
  }
  return true;
}

void printHelp(const std::string& lang) {
  const bool ru = (lang == "ru");
  std::cout << (ru ? "NexaClaw CLI (alias: clawforge)\n\n" : "NexaClaw CLI (alias: clawforge)\n\n");
  std::cout << "Usage:\n  nexaclaw [run] [--config <path>]\n  nexaclaw status|health|doctor|sessions\n  nexaclaw setup|onboard|configure [--wizard|--non-interactive]\n  nexaclaw gateway status|start|stop|restart|call\n  nexaclaw security audit [--deep] [--fix]\n  nexaclaw cron list\n  nexaclaw tools list\n  nexaclaw pairing list|approve <code>\n  nexaclaw config get <key>|set <key> <value>\n  nexaclaw models list|status|probe|set <provider/model|alias>\n  nexaclaw models aliases list|add|remove\n  nexaclaw models fallbacks list|add|remove|clear\n  nexaclaw models auth list|add|paste-token|setup-token|use|remove\n";
  std::cout << (ru ? "\nСовместимость OpenClaw: неизвестные top-level ветки отдаются как compatibility stub вместо unknown (см. docs/CLI_PARITY.md).\n"
                   : "\nOpenClaw compatibility: unknown top-level branches return compatibility stubs instead of hard unknown (see docs/CLI_PARITY.md).\n");
}

int runDoctor(const std::string& configPath, const std::string& lang) {
  const bool ru = (lang == "ru"); bool ok = true;
  auto report = [&](const std::string& t, bool p, const std::string& d) { std::cout << (p ? "[OK] " : "[FAIL] ") << t << (d.empty() ? "" : " — " + d) << std::endl; };
  report(ru ? "CLI язык" : "CLI language", true, ru ? "русский" : "English");
  if (std::filesystem::exists(configPath)) report(ru ? "Файл конфига" : "Config file", true, configPath); else { report(ru ? "Файл конфига" : "Config file", false, configPath); ok = false; }
  clawforge::core::AppConfig cfg; bool loaded = false;
  try { cfg = clawforge::core::AppConfig::loadFromFile(configPath); loaded = true; report("config.json parse", true, ""); } catch (const std::exception& e) { report("config.json parse", false, e.what()); ok = false; }
  if (loaded) {
    const char* key = std::getenv(cfg.model.apiKeyEnv.c_str());
    report(ru ? "Legacy LLM key" : "Legacy LLM key", key && std::string(key).size() > 0, cfg.model.apiKeyEnv);
    for (const auto& [name, p] : cfg.modelProviders) {
      const char* k = std::getenv(p.apiKeyEnv.c_str());
      report((ru ? "Провайдер модели " : "Model provider ") + name, k && std::string(k).size() > 0, p.apiKeyEnv);
    }
  }
  std::cout << (ok ? (ru ? "\nDoctor OK ✅" : "\nDoctor OK ✅") : (ru ? "\nDoctor: есть проблемы" : "\nDoctor: issues found")) << std::endl;
  return ok ? 0 : 1;
}

int initConfig(const std::string& configPath, const std::string& lang) {
  const auto dst = std::filesystem::path(configPath); const auto src = std::filesystem::path("config/config.example.json");
  if (std::filesystem::exists(dst)) { std::cout << (lang == "ru" ? "Конфиг уже существует: " : "Config already exists: ") << dst.string() << std::endl; return 0; }
  if (!std::filesystem::exists(src)) { std::cerr << "Template not found: " << src.string() << std::endl; return 1; }
  std::filesystem::create_directories(dst.parent_path()); std::filesystem::copy_file(src, dst); std::cout << "Created config: " << dst.string() << std::endl; return 0;
}

int runStatus(const std::string& configPath, [[maybe_unused]] const std::string& lang) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath); const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpGetJson(baseUrl + "/api/status", authHeaderFromEnv(cfg)); if (remote.has_value() && remote->value("ok", false)) { std::cout << remote->dump(2) << std::endl; return 0; }
  clawforge::session::SessionStore sessions(cfg.stateDir); sessions.init(); clawforge::core::EventBus bus; clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus); cron.init(); clawforge::tools::ToolRegistry tools; clawforge::tools::registerBuiltinTools(tools, cfg.workspace); tools.setPolicy(cfg.toolsPolicy);
  json local = {{"ok", true}, {"mode", "local"}, {"service", cfg.name}, {"sessions", {{"count", sessions.listSessions().size()}}}, {"jobs", {{"count", cron.listJobs().size()}}}, {"tools", {{"count", tools.list().size()}}}};
  std::cout << local.dump(2) << std::endl; return 0;
}

int runHealth(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath); const auto remote = httpGetJson("http://" + cfg.http.host + ":" + std::to_string(cfg.http.port) + "/health", authHeaderFromEnv(cfg));
  if (!remote.has_value()) { std::cout << json({{"ok", false}, {"error", "health endpoint unreachable"}}).dump(2) << std::endl; return 1; }
  std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1;
}

int runSessions(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath); const auto remote = httpGetJson("http://" + cfg.http.host + ":" + std::to_string(cfg.http.port) + "/api/sessions", authHeaderFromEnv(cfg));
  if (remote.has_value() && remote->value("ok", false)) { std::cout << remote->dump(2) << std::endl; return 0; }
  clawforge::session::SessionStore sessions(cfg.stateDir); sessions.init(); json arr = json::array(); for (const auto& s : sessions.listSessions()) arr.push_back({{"key", s.key}, {"sessionId", s.sessionId}, {"updatedAt", s.updatedAt}});
  std::cout << json({{"ok", true}, {"mode", "local"}, {"sessions", arr}}).dump(2) << std::endl; return 0;
}

int runCronList(const std::string& configPath) { const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath); const auto remote = httpGetJson("http://" + cfg.http.host + ":" + std::to_string(cfg.http.port) + "/api/cron/jobs", authHeaderFromEnv(cfg)); if (remote.has_value() && remote->value("ok", false)) { std::cout << remote->dump(2) << std::endl; return 0; } clawforge::core::EventBus bus; clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus); if (!cron.init()) return 1; json arr = json::array(); for (const auto& job : cron.listJobs()) arr.push_back({{"id", job.id}, {"name", job.name}, {"kind", job.kind}, {"nextRunAt", job.nextRunAt}, {"enabled", job.enabled}}); std::cout << json({{"ok", true}, {"mode", "local"}, {"jobs", arr}}).dump(2) << std::endl; return 0; }
int runToolsList(const std::string& configPath) { const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath); const auto remote = httpGetJson("http://" + cfg.http.host + ":" + std::to_string(cfg.http.port) + "/api/tools", authHeaderFromEnv(cfg)); if (remote.has_value() && remote->value("ok", false)) { std::cout << remote->dump(2) << std::endl; return 0; } clawforge::tools::ToolRegistry tools; clawforge::tools::registerBuiltinTools(tools, cfg.workspace); tools.setPolicy(cfg.toolsPolicy); std::cout << json({{"ok", true}, {"mode", "local"}, {"tools", tools.list()}, {"allowedTools", tools.allowedTools()}}).dump(2) << std::endl; return 0; }

int runPairingList(const std::string& configPath) { const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath); clawforge::channels::TelegramPairingStore store(cfg.stateDir); if (!store.init()) return 1; std::cout << "Pairing requests:\n"; for (const auto& req : store.listRequests()) std::cout << "- code=" << req.code << " userId=" << req.userId << " chatId=" << req.chatId << " approved=" << (req.approved ? "yes" : "no") << "\n"; std::cout << "\nApproved:\n"; for (const auto& req : store.listApproved()) std::cout << "- userId=" << req.userId << " code=" << req.code << " approvedAt=" << req.approvedAt << "\n"; return 0; }
int runPairingApprove(const std::string& configPath, const std::string& code) { const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath); clawforge::channels::TelegramPairingStore store(cfg.stateDir); if (!store.init()) return 1; const auto approved = store.approveByCode(code); if (!approved.has_value()) return 1; std::cout << "Approved: userId=" << approved->userId << " code=" << approved->code << std::endl; return 0; }

int runModelsList(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  json out = { {"ok", true}, {"current", cfg.modelRouting.current}, {"models", json::array()}, {"aliases", cfg.modelRouting.aliases}, {"fallbacks", cfg.modelRouting.fallbacks} };
  for (const auto& [name, m] : cfg.models) out["models"].push_back({{"name", name}, {"provider", m.provider}, {"model", m.model}});
  std::cout << out.dump(2) << std::endl; return 0;
}
int runModelsStatus(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  json providers = json::array();
  for (const auto& [name, p] : cfg.modelProviders) {
    const auto auth = clawforge::models::AuthProfileStore::resolveForProvider(cfg.stateDir, name, p.apiKeyEnv);
    providers.push_back({{"provider", name},
                         {"apiStyle", p.apiStyle},
                         {"endpoint", p.endpoint},
                         {"apiKeyEnv", p.apiKeyEnv},
                         {"keyPresent", auth.source != "missing"},
                         {"authSource", auth.source},
                         {"authMode", auth.mode},
                         {"profileId", auth.profileId},
                         {"expiresAt", auth.expiresAt},
                         {"expired", auth.expired},
                         {"warnings", auth.warnings}});
  }
  std::cout << json({{"ok", true}, {"current", cfg.modelRouting.current}, {"providers", providers}}).dump(2) << std::endl; return 0;
}

int runModelsAuth(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  clawforge::models::AuthProfileStore store(cfg.stateDir);
  if (!store.init()) { std::cerr << "Cannot init auth profile store" << std::endl; return 1; }

  if (pos.size() >= 3 && pos[2] == "list") {
    json arr = json::array();
    for (const auto& p : store.list()) {
      arr.push_back({{"id", p.id}, {"provider", p.provider}, {"mode", p.mode}, {"expiresAt", p.expiresAt},
                     {"expired", clawforge::models::AuthProfileStore::isExpired(p.expiresAt)}, {"meta", p.meta}, {"token", "***"}});
    }
    std::cout << json{{"ok", true}, {"profiles", arr}}.dump(2) << std::endl;
    return 0;
  }

  if (pos.size() >= 3 && pos[2] == "add") {
    const auto provider = argValue(pos, "--provider");
    const auto profileId = argValue(pos, "--profile-id");
    const auto apiKeyEnv = argValue(pos, "--api-key-env");
    const auto tokenEnv = argValue(pos, "--token-env");
    if (!provider.has_value() || !profileId.has_value() || (!apiKeyEnv.has_value() && !tokenEnv.has_value())) {
      std::cerr << "Usage: models auth add --provider <name> --profile-id <id> --api-key-env <ENV>|--token-env <ENV>" << std::endl;
      return 1;
    }
    const std::string envName = apiKeyEnv.has_value() ? *apiKeyEnv : *tokenEnv;
    const char* envVal = std::getenv(envName.c_str());
    if (!envVal || std::string(envVal).empty()) { std::cerr << "Env is empty or missing: " << envName << std::endl; return 1; }
    clawforge::models::AuthProfile p;
    p.id = *profileId; p.provider = *provider; p.mode = apiKeyEnv.has_value() ? "api_key" : "oauth_token"; p.token = envVal; p.meta = json{{"sourceEnv", envName}};
    if (!store.upsert(p)) return 1;
    std::cout << json{{"ok", true}, {"profileId", p.id}, {"provider", p.provider}, {"mode", p.mode}}.dump(2) << std::endl;
    return 0;
  }

  if (pos.size() >= 3 && pos[2] == "paste-token") {
    const auto provider = argValue(pos, "--provider");
    const auto profileId = argValue(pos, "--profile-id");
    auto token = argValue(pos, "--token");
    const auto expiresIn = argValue(pos, "--expires-in");
    if (!provider.has_value() || !profileId.has_value() || !token.has_value()) {
      std::cerr << "Usage: models auth paste-token --provider <name> --profile-id <id> --token <value> [--expires-in seconds]" << std::endl;
      return 1;
    }
    clawforge::models::AuthProfile p;
    p.id = *profileId; p.provider = *provider; p.mode = "oauth_token"; p.token = *token; p.meta = json{{"setup", "paste-token"}};
    if (expiresIn.has_value()) p.expiresAt = clawforge::models::AuthProfileStore::addSecondsUtcRfc3339(std::stoi(*expiresIn));
    if (!store.upsert(p)) return 1;
    std::cout << json{{"ok", true}, {"profileId", p.id}, {"provider", p.provider}, {"mode", p.mode}, {"expiresAt", p.expiresAt}}.dump(2) << std::endl;
    return 0;
  }

  if (pos.size() >= 3 && pos[2] == "setup-token") {
    const auto provider = argValue(pos, "--provider");
    const auto profileId = argValue(pos, "--profile-id");
    auto token = argValue(pos, "--token");
    const auto expiresIn = argValue(pos, "--expires-in");
    if (!provider.has_value() || *provider != "openai-codex") {
      std::cerr << "setup-token baseline currently supports --provider openai-codex" << std::endl;
      return 1;
    }
    std::string id = profileId.value_or("openai-codex-default");
    if (!token.has_value()) {
      std::cout << "Paste OAuth token for openai-codex (input hidden by terminal settings is not guaranteed in baseline): ";
      std::string in; std::getline(std::cin, in);
      token = in;
    }
    if (!token.has_value() || token->empty()) {
      std::cerr << "No token provided. Device-code OAuth flow is not implemented yet; obtain token externally and pass --token." << std::endl;
      return 1;
    }
    clawforge::models::AuthProfile p;
    p.id = id; p.provider = *provider; p.mode = "oauth_token"; p.token = *token;
    p.meta = json{{"setup", "setup-token"}, {"note", "Device-code flow is not implemented in Stage11 baseline; token was provided manually."}};
    if (expiresIn.has_value()) p.expiresAt = clawforge::models::AuthProfileStore::addSecondsUtcRfc3339(std::stoi(*expiresIn));
    if (!store.upsert(p)) return 1;
    std::cout << json{{"ok", true}, {"provider", p.provider}, {"profileId", p.id}, {"mode", p.mode}, {"expiresAt", p.expiresAt},
                      {"warning", "Stored manual token; full OAuth device-code flow remains roadmap."}}.dump(2) << std::endl;
    return 0;
  }

  if (pos.size() >= 3 && pos[2] == "use") {
    const auto provider = argValue(pos, "--provider");
    const auto profileId = argValue(pos, "--profile-id");
    if (!provider.has_value() || !profileId.has_value()) { std::cerr << "Usage: models auth use --provider <name> --profile-id <id>" << std::endl; return 1; }
    if (!store.setActive(*provider, *profileId)) { std::cerr << "Cannot set active profile (not found or provider mismatch)" << std::endl; return 1; }
    std::cout << json{{"ok", true}, {"provider", *provider}, {"activeProfileId", *profileId}}.dump(2) << std::endl;
    return 0;
  }

  if (pos.size() >= 3 && pos[2] == "remove") {
    const auto profileId = argValue(pos, "--profile-id");
    if (!profileId.has_value()) { std::cerr << "Usage: models auth remove --profile-id <id>" << std::endl; return 1; }
    if (!store.remove(*profileId)) { std::cerr << "Profile not found: " << *profileId << std::endl; return 1; }
    std::cout << json{{"ok", true}, {"removed", *profileId}}.dump(2) << std::endl;
    return 0;
  }

  std::cerr << "Unknown models auth subcommand" << std::endl;
  return 1;
}

int runModelsSet(const std::string& configPath, const std::string& value) { auto j = loadJsonFile(configPath); j["modelsConfig"]["routing"]["current"] = value; saveJsonFile(configPath, j); std::cout << "Current model set to " << value << std::endl; return 0; }
int runModelsAliases(const std::string& configPath, const std::string& action, const std::string& a = "", const std::string& b = "") {
  auto j = loadJsonFile(configPath); auto& aliases = j["modelsConfig"]["routing"]["aliases"]; if (!aliases.is_object()) aliases = json::object();
  if (action == "list") { std::cout << aliases.dump(2) << std::endl; return 0; }
  if (action == "add") { aliases[a] = b; saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0; }
  if (action == "remove") { aliases.erase(a); saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0; }
  return 1;
}
int runModelsFallbacks(const std::string& configPath, const std::string& action, const std::string& value = "") {
  auto j = loadJsonFile(configPath); auto& arr = j["modelsConfig"]["routing"]["fallbacks"]; if (!arr.is_array()) arr = json::array();
  if (action == "list") { std::cout << arr.dump(2) << std::endl; return 0; }
  if (action == "add") { arr.push_back(value); saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0; }
  if (action == "remove") { json out = json::array(); for (const auto& v : arr) if (v.get<std::string>() != value) out.push_back(v); arr = out; saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0; }
  if (action == "clear") { arr = json::array(); saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0; }
  return 1;
}

std::optional<std::string> parseJsonArg(const std::vector<std::string>& pos) {
  for (size_t i = 0; i + 1 < pos.size(); ++i) if (pos[i] == "--json") return pos[i + 1];
  return std::nullopt;
}

int runBrowserStatus(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpGetJson(baseUrl + "/api/browser/status", authHeaderFromEnv(cfg));
  if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }
  clawforge::browser::BrowserRelay relay(cfg.browser);
  std::cout << relay.status().dump(2) << std::endl;
  return 0;
}

int runBrowserOpen(const std::string& configPath, const std::string& url) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(baseUrl + "/api/browser/open", json{{"url", url}}, authHeaderFromEnv(cfg));
  if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }
  clawforge::browser::BrowserRelay relay(cfg.browser);
  const auto out = relay.open(url);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}

int runBrowserSnapshot(const std::string& configPath, const std::string& urlHint) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(baseUrl + "/api/browser/snapshot", json{{"url", urlHint}}, authHeaderFromEnv(cfg));
  if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }
  clawforge::browser::BrowserRelay relay(cfg.browser);
  const auto out = relay.snapshot(urlHint);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}

int runCronAction(const std::string& configPath, const std::string& action, const std::string& arg = "") {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string base = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto auth = authHeaderFromEnv(cfg);
  if (action == "list") return runCronList(configPath);
  if (action == "add" || action == "validate") {
    auto payload = json::parse(arg, nullptr, false);
    if (payload.is_discarded()) { std::cerr << "Invalid JSON payload" << std::endl; return 1; }
    const auto remote = httpPostJson(base + (action == "add" ? "/api/cron/jobs" : "/api/cron/validate"), payload, auth);
    if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }
    clawforge::core::EventBus bus; clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus); if (!cron.init()) return 1;
    const auto out = (action == "add") ? cron.addJob(payload) : cron.validate(payload);
    std::cout << out.dump(2) << std::endl; return out.value("ok", false) ? 0 : 1;
  }
  if (action == "rm" || action == "run") {
    const auto remote = (action == "rm") ? httpDeleteJson(base + "/api/cron/jobs/" + arg, auth) : httpPostJson(base + "/api/cron/jobs/" + arg + "/run-now", json::object(), auth);
    if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }
    clawforge::core::EventBus bus; clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus); if (!cron.init()) return 1;
    json out = (action == "rm") ? json{{"ok", cron.removeJob(arg)}, {"id", arg}} : cron.runNow(arg);
    std::cout << out.dump(2) << std::endl; return out.value("ok", false) ? 0 : 1;
  }
  return 1;
}

int runToolsCall(const std::string& configPath, const std::string& name, const std::string& payloadRaw) {
  auto payload = json::parse(payloadRaw, nullptr, false);
  if (payload.is_discarded()) { std::cerr << "Invalid JSON payload" << std::endl; return 1; }
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string base = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(base + "/api/tools/" + name, payload, authHeaderFromEnv(cfg));
  if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }
  clawforge::tools::ToolRegistry tools; clawforge::tools::registerBuiltinTools(tools, cfg.workspace); tools.setPolicy(cfg.toolsPolicy);
  const auto out = tools.call(name, payload); std::cout << out.dump(2) << std::endl; return out.value("ok", false) ? 0 : 1;
}

int runConfigGet(const std::string& configPath, const std::string& key) {
  auto j = loadJsonFile(configPath);
  if (key == "model.current") std::cout << j["modelsConfig"]["routing"].value("current", "") << std::endl;
  else if (key == "http.port") std::cout << j["http"].value("port", 0) << std::endl;
  else if (key == "gateway.auth.mode") std::cout << j["gateway"]["auth"].value("mode", "") << std::endl;
  else if (key == "gateway.auth.tokenEnv") std::cout << j["gateway"]["auth"].value("tokenEnv", "") << std::endl;
  else if (key == "api.dmScope") std::cout << j["api"].value("dmScope", "") << std::endl;
  else if (key == "telegram.dmPolicy") std::cout << j["telegram"].value("dmPolicy", "") << std::endl;
  else if (key == "models.routing.current") std::cout << j["modelsConfig"]["routing"].value("current", "") << std::endl;
  else if (key == "models.routing.aliases") std::cout << j["modelsConfig"]["routing"]["aliases"].dump(2) << std::endl;
  else if (key == "models.routing.fallbacks") std::cout << j["modelsConfig"]["routing"]["fallbacks"].dump(2) << std::endl;
  else if (key == "models.routing.image") std::cout << j["modelsConfig"]["routing"].value("image", "") << std::endl;
  else if (key == "models.routing.imageFallbacks") std::cout << j["modelsConfig"]["routing"]["imageFallbacks"].dump(2) << std::endl;
  else { std::cerr << "Unsupported config key baseline: " << key << std::endl; return 1; }
  return 0;
}
int runConfigSet(const std::string& configPath, const std::string& key, const std::string& value) {
  auto j = loadJsonFile(configPath);
  if (key == "model.current" || key == "models.routing.current") j["modelsConfig"]["routing"]["current"] = value;
  else if (key == "http.port") j["http"]["port"] = std::stoi(value);
  else if (key == "gateway.auth.mode") j["gateway"]["auth"]["mode"] = value;
  else if (key == "gateway.auth.tokenEnv") j["gateway"]["auth"]["tokenEnv"] = value;
  else if (key == "api.dmScope") j["api"]["dmScope"] = value;
  else if (key == "telegram.dmPolicy") j["telegram"]["dmPolicy"] = value;
  else if (key == "models.routing.image") j["modelsConfig"]["routing"]["image"] = value;
  else { std::cerr << "Unsupported config key baseline: " << key << std::endl; return 1; }
  saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0;
}

int runModelsProbe(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  json checks = json::array(); bool ok = true;
  for (const auto& [name, p] : cfg.modelProviders) {
    const auto auth = clawforge::models::AuthProfileStore::resolveForProvider(cfg.stateDir, name, p.apiKeyEnv);
    const bool present = !auth.token.empty();
    checks.push_back({{"provider", name}, {"endpoint", p.endpoint}, {"apiStyle", p.apiStyle}, {"apiKeyEnv", p.apiKeyEnv},
                      {"keyPresent", present}, {"authSource", auth.source}, {"authMode", auth.mode}, {"profileId", auth.profileId},
                      {"expiresAt", auth.expiresAt}, {"expired", auth.expired}, {"warnings", auth.warnings}});
    if (!present) ok = false;
  }
  std::cout << json{{"ok", ok}, {"current", cfg.modelRouting.current}, {"checks", checks}, {"note", "No token-spending remote calls executed"}}.dump(2) << std::endl;
  return ok ? 0 : 1;
}

int runModelsSetImage(const std::string& configPath, const std::string& value) {
  auto j = loadJsonFile(configPath);
  j["modelsConfig"]["routing"]["image"] = value;
  saveJsonFile(configPath, j);
  std::cout << "Image model set to " << value << std::endl;
  return 0;
}

int runImageFallbacks(const std::string& configPath, const std::string& action, const std::string& value = "") {
  auto j = loadJsonFile(configPath);
  auto& arr = j["modelsConfig"]["routing"]["imageFallbacks"];
  if (!arr.is_array()) arr = json::array();
  if (action == "list") { std::cout << arr.dump(2) << std::endl; return 0; }
  if (action == "add") { arr.push_back(value); saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0; }
  if (action == "remove") { json out = json::array(); for (const auto& v : arr) if (v.get<std::string>() != value) out.push_back(v); arr = out; saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0; }
  if (action == "clear") { arr = json::array(); saveJsonFile(configPath, j); std::cout << "OK" << std::endl; return 0; }
  return 1;
}

int runLogsTail(const std::string& configPath, int lines) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::filesystem::path p = cfg.audit.file;
  if (!std::filesystem::exists(p)) { std::cerr << "Audit log file not found: " << p.string() << std::endl; return 1; }
  std::ifstream in(p);
  std::vector<std::string> all; std::string line;
  while (std::getline(in, line)) all.push_back(line);
  const size_t start = all.size() > static_cast<size_t>(lines) ? all.size() - static_cast<size_t>(lines) : 0;
  for (size_t i = start; i < all.size(); ++i) std::cout << all[i] << "\n";
  return 0;
}

json gatewayStatusPayload(const clawforge::core::AppConfig& cfg) {
  const std::string base = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto health = httpGetJson(base + "/health", authHeaderFromEnv(cfg));
  const auto status = httpGetJson(base + "/api/status", authHeaderFromEnv(cfg));
  const auto pidFile = gatewayPidFile(cfg);
  const auto logFile = gatewayLogFile(cfg);
  const auto pid = readPidFile(pidFile);
  const bool pidAlive = pid.has_value() && pidRunning(*pid);
  const bool reachable = health.has_value() && health->value("ok", false);
  return {
      {"ok", true},
      {"reachable", reachable},
      {"running", reachable || pidAlive},
      {"pid", pid.value_or("")},
      {"pidAlive", pidAlive},
      {"pidFile", pidFile.string()},
      {"logFile", logFile.string()},
      {"url", base},
      {"status", status.has_value() ? *status : json::object()},
  };
}

int runGatewayStatus(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  std::cout << gatewayStatusPayload(cfg).dump(2) << std::endl;
  return 0;
}

int runGatewayStart(const std::string& configPath, const std::string& programPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  auto before = gatewayStatusPayload(cfg);
  if (before.value("running", false)) {
    before["note"] = "already running";
    std::cout << before.dump(2) << std::endl;
    return 0;
  }

  const auto pidFile = gatewayPidFile(cfg);
  const auto logFile = gatewayLogFile(cfg);
  clawforge::util::FileUtil::ensureDir(pidFile.parent_path());
  clawforge::util::FileUtil::ensureDir(logFile.parent_path());

  std::string binary = programPath;
  if (binary.empty()) binary = "./build/nexaclaw";
  const std::string cmd =
      "nohup " + clawforge::util::Shell::quote(binary) +
      " run --config " + clawforge::util::Shell::quote(configPath) +
      " > " + clawforge::util::Shell::quote(logFile.string()) +
      " 2>&1 & echo $! > " + clawforge::util::Shell::quote(pidFile.string());
  const auto res = clawforge::util::Shell::run(cmd);
  if (res.exitCode != 0) {
    std::cout << json{{"ok", false}, {"error", "failed to start gateway process"}, {"details", res.output}}.dump(2) << std::endl;
    return 1;
  }

  bool up = false;
  for (int i = 0; i < 20; ++i) {
    auto st = gatewayStatusPayload(cfg);
    if (st.value("reachable", false)) { up = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }

  auto out = gatewayStatusPayload(cfg);
  out["started"] = true;
  out["ok"] = up || out.value("pidAlive", false);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}

int runGatewayStop(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const auto pidFile = gatewayPidFile(cfg);
  const auto pid = readPidFile(pidFile);
  if (!pid.has_value()) {
    auto out = gatewayStatusPayload(cfg);
    out["ok"] = true;
    out["stopped"] = false;
    out["note"] = "no pid file";
    std::cout << out.dump(2) << std::endl;
    return 0;
  }

  clawforge::util::Shell::run("kill " + clawforge::util::Shell::quote(*pid) + " >/dev/null 2>&1");
  bool alive = true;
  for (int i = 0; i < 20; ++i) {
    alive = pidRunning(*pid);
    if (!alive) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::error_code ec;
  std::filesystem::remove(pidFile, ec);
  auto out = gatewayStatusPayload(cfg);
  out["stopped"] = !alive;
  out["ok"] = !alive;
  out["killedPid"] = *pid;
  std::cout << out.dump(2) << std::endl;
  return alive ? 1 : 0;
}

int runGatewayRestart(const std::string& configPath, const std::string& programPath) {
  const int stopRc = runGatewayStop(configPath);
  const int startRc = runGatewayStart(configPath, programPath);
  return (stopRc == 0 && startRc == 0) ? 0 : 1;
}

int runGatewayCall(const std::string& configPath, const std::vector<std::string>& pos) {
  if (pos.size() < 3) {
    std::cerr << "Usage: nexaclaw gateway call <method> [--params <json>]" << std::endl;
    return 1;
  }

  const std::string method = pos[2];
  const std::string paramsRaw = argValue(pos, "--params").value_or("{}");
  auto params = json::parse(paramsRaw, nullptr, false);
  if (params.is_discarded()) {
    std::cout << json{{"ok", false}, {"error", "invalid --params json"}}.dump(2) << std::endl;
    return 1;
  }

  if (method == "health") return runHealth(configPath);
  if (method == "status") return runStatus(configPath, "en");

  if (method == "config.get") {
    const auto raw = clawforge::util::FileUtil::readText(configPath).value_or("{}");
    auto parsed = json::parse(raw, nullptr, false);
    if (parsed.is_discarded()) parsed = json::object();
    std::cout << json{{"ok", true}, {"payload", {{"path", configPath}, {"hash", jsonHash(parsed)}, {"raw", raw}}}}.dump(2) << std::endl;
    return 0;
  }

  if (method == "config.apply" || method == "config.patch") {
    const std::string raw = params.value("raw", "");
    const std::string baseHash = params.value("baseHash", "");
    if (raw.empty()) {
      std::cout << json{{"ok", false}, {"error", "raw is required"}}.dump(2) << std::endl;
      return 1;
    }

    json current = loadJsonFile(configPath);
    const std::string currentHash = jsonHash(current);
    if (!baseHash.empty() && baseHash != currentHash) {
      std::cout << json{{"ok", false}, {"error", "baseHash mismatch"}, {"currentHash", currentHash}}.dump(2) << std::endl;
      return 1;
    }

    auto incoming = json::parse(raw, nullptr, false);
    if (incoming.is_discarded()) {
      std::cout << json{{"ok", false}, {"error", "raw must be valid JSON"}}.dump(2) << std::endl;
      return 1;
    }

    json next = (method == "config.apply") ? incoming : mergePatch(current, incoming);
    std::string validationError;
    if (!validateConfigJson(next, validationError)) {
      std::cout << json{{"ok", false}, {"error", "config validation failed"}, {"details", validationError}}.dump(2) << std::endl;
      return 1;
    }

    saveJsonFile(configPath, next);

    const auto cfgAfter = clawforge::core::AppConfig::loadFromFile(configPath);
    const bool running = gatewayStatusPayload(cfgAfter).value("running", false);
    std::cout << json{{"ok", true},
                      {"method", method},
                      {"path", configPath},
                      {"hash", jsonHash(next)},
                      {"gatewayRunning", running},
                      {"note", running ? "Gateway restart recommended" : "Config saved"}}
                     .dump(2)
              << std::endl;
    return 0;
  }

  if (method == "update.run") {
    std::cout << json{{"ok", false}, {"error", "update.run is not implemented yet in NexaClaw baseline"}}.dump(2) << std::endl;
    return 2;
  }

  std::cout << json{{"ok", false}, {"error", "unsupported gateway call method"}, {"method", method}}.dump(2) << std::endl;
  return 1;
}

int runSecurityAudit(const std::string& configPath, const std::vector<std::string>& pos, const std::string& lang) {
  const bool ru = (lang == "ru");
  const bool deep = hasFlag(pos, "--deep");
  const bool fix = hasFlag(pos, "--fix");

  auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  auto conf = loadJsonFile(configPath);

  int warnings = 0;
  int fails = 0;
  int fixed = 0;
  auto report = [&](const std::string& title, const std::string& level, const std::string& details) {
    std::cout << "[" << level << "] " << title << (details.empty() ? "" : " — " + details) << std::endl;
  };

  if (cfg.api.dmScope == "main") {
    ++warnings;
    if (fix) {
      conf["api"]["dmScope"] = "per-channel-peer";
      saveJsonFile(configPath, conf);
      ++fixed;
      report(ru ? "Изоляция DM" : "DM isolation", "FIX", ru ? "api.dmScope -> per-channel-peer" : "api.dmScope -> per-channel-peer");
      cfg = clawforge::core::AppConfig::loadFromFile(configPath);
    } else {
      report(ru ? "Изоляция DM" : "DM isolation", "WARN", ru ? "api.dmScope=main (риск смешения контекста)" : "api.dmScope=main (context leakage risk)");
    }
  } else {
    report(ru ? "Изоляция DM" : "DM isolation", "OK", "api.dmScope=" + cfg.api.dmScope);
  }

  if (cfg.gateway.auth.mode == "token") {
    if (hasEnvToken(cfg.gateway.auth.tokenEnv)) report("gateway.auth.token", "OK", cfg.gateway.auth.tokenEnv);
    else { ++fails; report("gateway.auth.token", "FAIL", "missing env: " + cfg.gateway.auth.tokenEnv); }
  } else {
    if (!isLoopbackHost(cfg.http.host)) {
      ++warnings;
      report("gateway.auth", "WARN", ru ? "auth=off и host не loopback" : "auth=off with non-loopback host");
    } else {
      report("gateway.auth", "OK", "mode=off + loopback host");
    }
  }

  if (permsTooOpen(configPath)) {
    ++warnings;
    if (fix) { tightenPermsOwnerOnly(configPath); ++fixed; report(ru ? "Права config" : "Config permissions", "FIX", configPath); }
    else report(ru ? "Права config" : "Config permissions", "WARN", ru ? "слишком широкие" : "too open");
  } else {
    report(ru ? "Права config" : "Config permissions", "OK", configPath);
  }

  if (permsTooOpen(cfg.stateDir)) {
    ++warnings;
    if (fix) { tightenPermsOwnerOnly(cfg.stateDir); ++fixed; report(ru ? "Права stateDir" : "State dir permissions", "FIX", cfg.stateDir.string()); }
    else report(ru ? "Права stateDir" : "State dir permissions", "WARN", ru ? "слишком широкие" : "too open");
  } else {
    report(ru ? "Права stateDir" : "State dir permissions", "OK", cfg.stateDir.string());
  }

  if (deep) {
    const auto health = httpGetJson("http://" + cfg.http.host + ":" + std::to_string(cfg.http.port) + "/health", authHeaderFromEnv(cfg));
    if (health.has_value() && health->value("ok", false)) report("gateway probe", "OK", "/health reachable");
    else { ++warnings; report("gateway probe", "WARN", "/health unreachable"); }
  }

  std::cout << json{{"ok", fails == 0}, {"warnings", warnings}, {"fails", fails}, {"fixed", fixed}}.dump(2) << std::endl;
  return fails == 0 ? 0 : 1;
}

void applyRecommendedSetupDefaults(json& cfg) {
  cfg["name"] = "nexaclaw";
  cfg["gateway"]["auth"]["tokenEnv"] = "NEXACLAW_GATEWAY_TOKEN";
  cfg["api"]["dmScope"] = "per-channel-peer";
  cfg["telegram"]["dmPolicy"] = "pairing";
}

void printSetupSummary(const json& cfg, const std::string& lang) {
  const bool ru = (lang == "ru");
  const json gateway = (cfg.contains("gateway") && cfg["gateway"].is_object()) ? cfg["gateway"] : json::object();
  const json auth = (gateway.contains("auth") && gateway["auth"].is_object()) ? gateway["auth"] : json::object();
  const json api = (cfg.contains("api") && cfg["api"].is_object()) ? cfg["api"] : json::object();
  const json telegram = (cfg.contains("telegram") && cfg["telegram"].is_object()) ? cfg["telegram"] : json::object();
  const json model = (cfg.contains("model") && cfg["model"].is_object()) ? cfg["model"] : json::object();

  std::cout << "\n" << (ru ? "Текущие параметры:" : "Current settings:") << "\n";
  std::cout << "  - name: " << cfg.value("name", "nexaclaw") << "\n";
  std::cout << "  - gateway.auth.mode: " << auth.value("mode", "off") << "\n";
  std::cout << "  - gateway.auth.tokenEnv: " << auth.value("tokenEnv", "NEXACLAW_GATEWAY_TOKEN") << "\n";
  std::cout << "  - api.dmScope: " << api.value("dmScope", "main") << "\n";
  std::cout << "  - telegram.enabled: " << (telegram.value("enabled", false) ? "true" : "false") << "\n";
  std::cout << "  - telegram.dmPolicy: " << telegram.value("dmPolicy", "open") << "\n";
  std::cout << "  - model.apiKeyEnv: " << model.value("apiKeyEnv", "OPENAI_API_KEY") << "\n";
}

int runSetupWizard(const std::string& configPath, const std::string& lang, const std::vector<std::string>& pos,
                   const std::string& programPath, const std::string& invokedAs) {
  const bool ru = (lang == "ru");
  const bool nonInteractive = hasFlag(pos, "--non-interactive") || hasFlag(pos, "--yes");

  std::string err;
  if (!ensureConfigFile(configPath, err)) {
    std::cerr << err << std::endl;
    return 1;
  }

  json cfg = loadJsonFile(configPath);

  if (nonInteractive || !isatty(STDIN_FILENO)) {
    applyRecommendedSetupDefaults(cfg);
    saveJsonFile(configPath, cfg);
    std::cout << json{{"ok", true}, {"mode", "non-interactive"}, {"command", invokedAs}, {"config", configPath}}.dump(2) << std::endl;
    return 0;
  }

  std::cout << (ru ? "\nДобро пожаловать в NexaClaw Setup Wizard 🚀\n" : "\nWelcome to NexaClaw Setup Wizard 🚀\n");
  std::cout << (ru ? "Команда: " : "Command: ") << invokedAs << "\n";

  while (true) {
    printSetupSummary(cfg, lang);
    std::cout << "\n";
    std::cout << (ru ? "[1] Применить рекомендованные безопасные настройки\n" : "[1] Apply recommended safe defaults\n");
    std::cout << (ru ? "[2] Настроить gateway auth (off/token)\n" : "[2] Configure gateway auth (off/token)\n");
    std::cout << (ru ? "[3] Настроить DM scope (main/per-peer/per-channel-peer)\n" : "[3] Configure DM scope (main/per-peer/per-channel-peer)\n");
    std::cout << (ru ? "[4] Настроить Telegram (enabled + dmPolicy)\n" : "[4] Configure Telegram (enabled + dmPolicy)\n");
    std::cout << (ru ? "[5] Настроить модель (apiKeyEnv + current model)\n" : "[5] Configure model (apiKeyEnv + current model)\n");
    std::cout << (ru ? "[6] Сохранить и выйти\n" : "[6] Save and exit\n");
    std::cout << (ru ? "[7] Сохранить и запустить doctor\n" : "[7] Save and run doctor\n");
    std::cout << (ru ? "[8] Сохранить и запустить gateway\n" : "[8] Save and start gateway\n");
    std::cout << (ru ? "[0] Выход без сохранения\n" : "[0] Exit without saving\n");

    const std::string choice = promptLine(ru ? "Выбор" : "Choice", "6");
    if (choice == "0") {
      std::cout << (ru ? "Выход без изменений." : "Exit without changes.") << std::endl;
      return 0;
    }
    if (choice == "1") {
      applyRecommendedSetupDefaults(cfg);
      continue;
    }
    if (choice == "2") {
      const std::string mode = promptLine("gateway.auth.mode", cfg["gateway"]["auth"].value("mode", "off"));
      if (mode == "off" || mode == "token") cfg["gateway"]["auth"]["mode"] = mode;
      cfg["gateway"]["auth"]["tokenEnv"] = promptLine("gateway.auth.tokenEnv", cfg["gateway"]["auth"].value("tokenEnv", "NEXACLAW_GATEWAY_TOKEN"));
      continue;
    }
    if (choice == "3") {
      const std::string scope = promptLine("api.dmScope", cfg["api"].value("dmScope", "main"));
      if (scope == "main" || scope == "per-peer" || scope == "per-channel-peer") cfg["api"]["dmScope"] = scope;
      continue;
    }
    if (choice == "4") {
      const std::string enabled = promptLine("telegram.enabled (true/false)", cfg["telegram"].value("enabled", false) ? "true" : "false");
      cfg["telegram"]["enabled"] = (enabled == "true" || enabled == "1" || enabled == "yes");
      const std::string dmPolicy = promptLine("telegram.dmPolicy", cfg["telegram"].value("dmPolicy", "pairing"));
      if (dmPolicy == "open" || dmPolicy == "allowlist" || dmPolicy == "pairing" || dmPolicy == "disabled") cfg["telegram"]["dmPolicy"] = dmPolicy;
      continue;
    }
    if (choice == "5") {
      cfg["model"]["apiKeyEnv"] = promptLine("model.apiKeyEnv", cfg["model"].value("apiKeyEnv", "OPENAI_API_KEY"));
      cfg["modelsConfig"]["routing"]["current"] = promptLine("modelsConfig.routing.current", cfg["modelsConfig"]["routing"].value("current", "openai/gpt-4o-mini"));
      continue;
    }
    if (choice == "6" || choice == "7" || choice == "8") {
      saveJsonFile(configPath, cfg);
      std::cout << (ru ? "Конфиг сохранён: " : "Config saved: ") << configPath << std::endl;
      if (choice == "7") return runDoctor(configPath, lang);
      if (choice == "8") return runGatewayStart(configPath, programPath);
      return 0;
    }

    std::cout << (ru ? "Неизвестный пункт меню" : "Unknown menu item") << std::endl;
  }
}

int runSystemEvent(const std::string& configPath, const std::string& text) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string base = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(base + "/api/message", json{{"sessionKey", "main"}, {"text", text}, {"systemEvent", true}}, authHeaderFromEnv(cfg));
  if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }
  clawforge::session::SessionStore sessions(cfg.stateDir);
  sessions.init();
  sessions.ensureSession("main");
  sessions.appendMessage("main", "system", text);
  std::cout << json{{"ok", true}, {"mode", "local"}, {"sessionKey", "main"}, {"queued", true}, {"note", "Stored as system message in main session"}}.dump(2) << std::endl;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string command = "run", configPath = "config/config.json", lang = detectLang();
    const std::string programPath = (argc > 0 && argv && argv[0]) ? std::string(argv[0]) : "nexaclaw";
    std::vector<std::string> args; for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    std::vector<std::string> pos;
    for (size_t i = 0; i < args.size(); ++i) {
      if (args[i] == "--lang" && i + 1 < args.size()) lang = normalizeLang(args[++i]);
      else if (args[i] == "--config" && i + 1 < args.size()) configPath = args[++i];
      else if (args[i] == "--help" || args[i] == "-h") command = "help";
      else if (args[i] == "--doctor") command = "doctor";
      else if (args[i] == "--init-config") command = "init-config";
      else pos.push_back(args[i]);
    }

    if (!pos.empty() && command == "run") command = pos[0];

    if (command == "help") return (printHelp(lang), 0);
    if (command == "doctor") return runDoctor(configPath, lang);
    if (command == "init-config") return initConfig(configPath, lang);
    if (command == "status") return runStatus(configPath, lang);
    if (command == "health") return runHealth(configPath);
    if (command == "sessions") return runSessions(configPath);

    if (command == "setup" || command == "onboard" || command == "configure") {
      return runSetupWizard(configPath, lang, pos, programPath, command);
    }

    if (command == "gateway") {
      if (pos.size() >= 2 && pos[1] == "status") return runGatewayStatus(configPath);
      if (pos.size() >= 2 && pos[1] == "start") return runGatewayStart(configPath, programPath);
      if (pos.size() >= 2 && pos[1] == "stop") return runGatewayStop(configPath);
      if (pos.size() >= 2 && pos[1] == "restart") return runGatewayRestart(configPath, programPath);
      if (pos.size() >= 2 && pos[1] == "health") return runHealth(configPath);
      if (pos.size() >= 2 && pos[1] == "call") return runGatewayCall(configPath, pos);
      return (printCompatNotImplemented("gateway " + (pos.size() >= 2 ? pos[1] : ""), lang), 2);
    }

    if (command == "security") {
      if (pos.size() == 1 || (pos.size() >= 2 && pos[1] == "audit")) return runSecurityAudit(configPath, pos, lang);
      return (printCompatNotImplemented("security " + (pos.size() >= 2 ? pos[1] : ""), lang), 2);
    }

    if (command == "browser" && pos.size() >= 2 && pos[1] == "status") return runBrowserStatus(configPath);
    if (command == "browser" && pos.size() >= 3 && pos[1] == "open") return runBrowserOpen(configPath, pos[2]);
    if (command == "browser" && pos.size() >= 2 && pos[1] == "snapshot") return runBrowserSnapshot(configPath, pos.size() >= 3 ? pos[2] : "");
    if (command == "cron" && pos.size() >= 2 && pos[1] == "list") return runCronAction(configPath, "list");
    if (command == "cron" && pos.size() >= 2 && pos[1] == "add") { auto j = parseJsonArg(pos); if (!j.has_value()) { std::cerr << "Missing --json payload" << std::endl; return 1; } return runCronAction(configPath, "add", *j); }
    if (command == "cron" && pos.size() >= 3 && pos[1] == "rm") return runCronAction(configPath, "rm", pos[2]);
    if (command == "cron" && pos.size() >= 3 && pos[1] == "run") return runCronAction(configPath, "run", pos[2]);
    if (command == "cron" && pos.size() >= 2 && pos[1] == "validate") { auto j = parseJsonArg(pos); if (!j.has_value()) { std::cerr << "Missing --json payload" << std::endl; return 1; } return runCronAction(configPath, "validate", *j); }
    if (command == "tools" && pos.size() >= 2 && pos[1] == "list") return runToolsList(configPath);
    if (command == "tools" && pos.size() >= 4 && pos[1] == "call" && pos[3] == "--json") return runToolsCall(configPath, pos[2], pos.size() >= 5 ? pos[4] : "{}");
    if (command == "logs" && pos.size() >= 2 && pos[1] == "tail") return runLogsTail(configPath, pos.size() >= 3 ? std::max(1, std::stoi(pos[2])) : 50);
    if (command == "system" && pos.size() >= 3 && pos[1] == "event") { std::string text = pos[2]; for (size_t i = 3; i < pos.size(); ++i) text += " " + pos[i]; return runSystemEvent(configPath, text); }
    if (command == "pairing" && pos.size() >= 2 && pos[1] == "list") return runPairingList(configPath);
    if (command == "pairing" && pos.size() >= 3 && pos[1] == "approve") return runPairingApprove(configPath, pos[2]);
    if (command == "config" && pos.size() >= 3 && pos[1] == "get") return runConfigGet(configPath, pos[2]);
    if (command == "config" && pos.size() >= 4 && pos[1] == "set") return runConfigSet(configPath, pos[2], pos[3]);

    if (command == "models") {
      if (pos.size() >= 2 && pos[1] == "list") return runModelsList(configPath);
      if (pos.size() >= 2 && pos[1] == "status") return runModelsStatus(configPath);
      if (pos.size() >= 2 && pos[1] == "probe") return runModelsProbe(configPath);
      if (pos.size() >= 3 && pos[1] == "auth") return runModelsAuth(configPath, pos);
      if (pos.size() >= 3 && pos[1] == "set") return runModelsSet(configPath, pos[2]);
      if (pos.size() >= 3 && pos[1] == "set-image") return runModelsSetImage(configPath, pos[2]);
      if (pos.size() >= 3 && pos[1] == "aliases") {
        if (pos[2] == "list") return runModelsAliases(configPath, "list");
        if (pos.size() >= 5 && pos[2] == "add") return runModelsAliases(configPath, "add", pos[3], pos[4]);
        if (pos.size() >= 4 && pos[2] == "remove") return runModelsAliases(configPath, "remove", pos[3]);
      }
      if (pos.size() >= 3 && pos[1] == "fallbacks") {
        if (pos[2] == "list") return runModelsFallbacks(configPath, "list");
        if (pos.size() >= 4 && pos[2] == "add") return runModelsFallbacks(configPath, "add", pos[3]);
        if (pos.size() >= 4 && pos[2] == "remove") return runModelsFallbacks(configPath, "remove", pos[3]);
        if (pos[2] == "clear") return runModelsFallbacks(configPath, "clear");
      }
      std::cerr << "Unknown models subcommand" << std::endl; return 1;
    }

    if (command == "image-fallbacks" && pos.size() >= 2) {
      if (pos[1] == "list") return runImageFallbacks(configPath, "list");
      if (pos.size() >= 3 && pos[1] == "add") return runImageFallbacks(configPath, "add", pos[2]);
      if (pos.size() >= 3 && pos[1] == "remove") return runImageFallbacks(configPath, "remove", pos[2]);
      if (pos[1] == "clear") return runImageFallbacks(configPath, "clear");
      std::cerr << "Unknown image-fallbacks subcommand" << std::endl; return 1;
    }

    std::set<std::string> compatTop = {"dashboard","reset","uninstall","update","message","agent","agents","acp","memory","nodes","devices","node","approvals","sandbox","dns","docs","hooks","webhooks","plugins","channels","skills","tui","voicecall","directory"};
    if (compatTop.count(command)) return (printCompatNotImplemented(command, lang), 2);

    if (command != "run") {
      std::cerr << (lang == "ru" ? "Неизвестная команда: " : "Unknown command: ") << command << std::endl;
      printHelp(lang);
      return 1;
    }

    auto config = clawforge::core::AppConfig::loadFromFile(configPath);
    clawforge::app::Application app(std::move(config), lang); g_app = &app;
    std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal);
    return app.run();
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
