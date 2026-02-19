#include <algorithm>
#include <csignal>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
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
std::optional<json> httpPatchJson(const std::string& url, const json& body, const std::string& authHeader = "") { return httpRequestJson("PATCH", url, authHeader, body); }
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
  for (size_t i = 0; i + 1 < pos.size(); ++i)
    if (pos[i] == key) return pos[i + 1];
  return std::nullopt;
}

std::vector<std::string> argValues(const std::vector<std::string>& pos, const std::string& key) {
  std::vector<std::string> out;
  for (size_t i = 0; i + 1 < pos.size(); ++i) {
    if (pos[i] != key) continue;
    out.push_back(pos[i + 1]);
    ++i;
  }
  return out;
}

bool hasFlag(const std::vector<std::string>& pos, const std::string& flag) {
  for (const auto& x : pos)
    if (x == flag) return true;
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
  std::cout << "Usage:\n  nexaclaw [run] [--config <path>]\n  nexaclaw status|health|doctor|sessions\n  nexaclaw setup|onboard|configure [--wizard|--non-interactive]\n  nexaclaw gateway run|status|start|stop|restart|probe|call\n  nexaclaw security audit [--deep] [--fix]\n  nexaclaw cron status|list|get|add|edit|enable|disable|run|runs|validate|rm\n  nexaclaw browser status|open|navigate|snapshot|click|type|screenshot\n  nexaclaw nodes|node list|status|describe|invoke\n  nexaclaw devices list|status|invoke\n  nexaclaw canvas status|list|snapshot|invoke\n  nexaclaw message send|react|delete|poll --channel telegram ...\n  nexaclaw channels list|status|capabilities|resolve|add|remove\n  nexaclaw tools list\n  nexaclaw pairing list|approve <code>\n  nexaclaw config get <key>|set <key> <value>\n  nexaclaw models list|status|probe|set <provider/model|alias>\n  nexaclaw models aliases list|add|remove\n  nexaclaw models fallbacks list|add|remove|clear\n  nexaclaw models auth list|add|login|paste-token|setup-token|use|remove\n  nexaclaw models auth order get|set|clear --provider <name>\n";
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



int64_t nowMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::filesystem::path agentsRegistryPath(const clawforge::core::AppConfig& cfg) {
  return cfg.stateDir / "agents" / "agents.json";
}

bool validAgentId(const std::string& id) {
  if (id.empty() || id.size() > 64) return false;
  static const std::regex kId(R"(^[A-Za-z0-9._-]+$)");
  return std::regex_match(id, kId);
}

json defaultAgentsRegistry() {
  return json{{"version", 1},
              {"active", "main"},
              {"agents", json::array({json{{"id", "main"},
                                             {"name", "Main"},
                                             {"sessionKey", "main"},
                                             {"createdAtMs", nowMillis()}}})}};
}

json normalizeAgentsRegistry(json reg) {
  if (!reg.is_object()) reg = json::object();
  if (!reg.contains("version")) reg["version"] = 1;
  if (!reg.contains("agents") || !reg["agents"].is_array()) reg["agents"] = json::array();

  bool hasMain = false;
  json normalized = json::array();
  std::set<std::string> seen;
  for (const auto& row : reg["agents"]) {
    const std::string id = row.value("id", "");
    if (!validAgentId(id) || seen.count(id)) continue;
    seen.insert(id);
    json item = row;
    if (!item.contains("name") || !item["name"].is_string()) item["name"] = id;
    if (!item.contains("sessionKey") || !item["sessionKey"].is_string() || item["sessionKey"].get<std::string>().empty()) {
      item["sessionKey"] = (id == "main" ? "main" : "agent:" + id);
    }
    if (!item.contains("createdAtMs")) item["createdAtMs"] = nowMillis();
    normalized.push_back(item);
    if (id == "main") hasMain = true;
  }
  if (!hasMain) {
    normalized.push_back(json{{"id", "main"}, {"name", "Main"}, {"sessionKey", "main"}, {"createdAtMs", nowMillis()}});
  }
  reg["agents"] = normalized;

  const std::string active = reg.value("active", "main");
  bool activeExists = false;
  for (const auto& row : reg["agents"]) if (row.value("id", "") == active) activeExists = true;
  reg["active"] = activeExists ? active : "main";
  return reg;
}

json loadAgentsRegistry(const clawforge::core::AppConfig& cfg) {
  const auto path = agentsRegistryPath(cfg);
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  json reg;
  if (!std::filesystem::exists(path)) {
    reg = defaultAgentsRegistry();
    saveJsonFile(path.string(), reg);
    return reg;
  }

  auto parsed = json::parse(clawforge::util::FileUtil::readText(path).value_or("{}"), nullptr, false);
  if (parsed.is_discarded()) parsed = defaultAgentsRegistry();
  reg = normalizeAgentsRegistry(parsed);
  saveJsonFile(path.string(), reg);
  return reg;
}

void saveAgentsRegistry(const clawforge::core::AppConfig& cfg, const json& reg) {
  saveJsonFile(agentsRegistryPath(cfg).string(), normalizeAgentsRegistry(reg));
}

std::optional<json> findAgent(const json& reg, const std::string& id) {
  if (!reg.contains("agents") || !reg["agents"].is_array()) return std::nullopt;
  for (const auto& row : reg["agents"]) {
    if (row.value("id", "") == id) return row;
  }
  return std::nullopt;
}

int printNotImplJson(const std::string& top, const std::string& sub, const std::vector<std::string>& available) {
  std::cout << json{{"ok", false},
                    {"error", "not_implemented"},
                    {"command", top},
                    {"subcommand", sub},
                    {"available", available},
                    {"note", "Compatibility stub: subcommand is not implemented in NexaClaw yet"}}
                   .dump(2)
            << std::endl;
  return 2;
}

int runAgentsFamily(const std::string& configPath, const std::vector<std::string>& pos, const std::string& top) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  auto reg = loadAgentsRegistry(cfg);
  const std::vector<std::string> available = {"list", "show", "create", "delete", "use", "run"};

  std::string sub = "list";
  if (pos.size() >= 2) sub = pos[1];

  if (sub == "list") {
    std::cout << json{{"ok", true}, {"command", top}, {"active", reg.value("active", "main")}, {"agents", reg["agents"]}}.dump(2) << std::endl;
    return 0;
  }

  if (sub == "show" || sub == "get") {
    std::string id = pos.size() >= 3 ? pos[2] : reg.value("active", "main");
    auto agent = findAgent(reg, id);
    if (!agent.has_value()) {
      std::cout << json{{"ok", false}, {"error", "agent_not_found"}, {"agentId", id}}.dump(2) << std::endl;
      return 1;
    }
    std::cout << json{{"ok", true}, {"command", top}, {"active", reg.value("active", "main")}, {"agent", *agent}}.dump(2) << std::endl;
    return 0;
  }

  if (sub == "create" || sub == "add") {
    if (pos.size() < 3) {
      std::cout << json{{"ok", false}, {"error", "usage"}, {"usage", top + " create <agent-id> [--name <display-name>] [--session-key <session>]"}}.dump(2) << std::endl;
      return 1;
    }
    const std::string id = pos[2];
    if (!validAgentId(id)) {
      std::cout << json{{"ok", false}, {"error", "invalid_agent_id"}, {"agentId", id}}.dump(2) << std::endl;
      return 1;
    }
    if (findAgent(reg, id).has_value()) {
      std::cout << json{{"ok", false}, {"error", "agent_exists"}, {"agentId", id}}.dump(2) << std::endl;
      return 1;
    }
    const std::string name = argValue(pos, "--name").value_or(id);
    const std::string sessionKey = argValue(pos, "--session-key").value_or("agent:" + id);
    reg["agents"].push_back(json{{"id", id}, {"name", name}, {"sessionKey", sessionKey}, {"createdAtMs", nowMillis()}});
    saveAgentsRegistry(cfg, reg);
    std::cout << json{{"ok", true}, {"created", true}, {"agent", findAgent(reg, id).value()}}.dump(2) << std::endl;
    return 0;
  }

  if (sub == "delete" || sub == "rm" || sub == "remove") {
    if (pos.size() < 3) {
      std::cout << json{{"ok", false}, {"error", "usage"}, {"usage", top + " delete <agent-id>"}}.dump(2) << std::endl;
      return 1;
    }
    const std::string id = pos[2];
    if (id == "main") {
      std::cout << json{{"ok", false}, {"error", "protected_agent"}, {"agentId", id}}.dump(2) << std::endl;
      return 1;
    }
    json kept = json::array();
    bool removed = false;
    for (const auto& row : reg["agents"]) {
      if (row.value("id", "") == id) {
        removed = true;
        continue;
      }
      kept.push_back(row);
    }
    if (!removed) {
      std::cout << json{{"ok", false}, {"error", "agent_not_found"}, {"agentId", id}}.dump(2) << std::endl;
      return 1;
    }
    reg["agents"] = kept;
    if (reg.value("active", "main") == id) reg["active"] = "main";
    saveAgentsRegistry(cfg, reg);
    std::cout << json{{"ok", true}, {"deleted", true}, {"agentId", id}, {"active", reg.value("active", "main")}}.dump(2) << std::endl;
    return 0;
  }

  if (sub == "use") {
    if (pos.size() < 3) {
      std::cout << json{{"ok", false}, {"error", "usage"}, {"usage", top + " use <agent-id>"}}.dump(2) << std::endl;
      return 1;
    }
    const std::string id = pos[2];
    if (!findAgent(reg, id).has_value()) {
      std::cout << json{{"ok", false}, {"error", "agent_not_found"}, {"agentId", id}}.dump(2) << std::endl;
      return 1;
    }
    reg["active"] = id;
    saveAgentsRegistry(cfg, reg);
    std::cout << json{{"ok", true}, {"active", id}}.dump(2) << std::endl;
    return 0;
  }

  if (sub == "run") {
    std::string id = argValue(pos, "--agent").value_or(reg.value("active", "main"));
    if (pos.size() >= 3 && !pos[2].empty() && pos[2].rfind("--", 0) != 0) id = pos[2];
    const auto agent = findAgent(reg, id);
    if (!agent.has_value()) {
      std::cout << json{{"ok", false}, {"error", "agent_not_found"}, {"agentId", id}}.dump(2) << std::endl;
      return 1;
    }
    const auto message = argValue(pos, "--message");
    if (!message.has_value() || message->empty()) {
      std::cout << json{{"ok", false}, {"error", "usage"}, {"usage", top + " run [<agent-id>|--agent <id>] --message <text> [--timeout-ms <ms>]"}}.dump(2) << std::endl;
      return 1;
    }

    const std::string base = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
    int timeoutMs = 30000;
    try {
      timeoutMs = std::max(1000, std::stoi(argValue(pos, "--timeout-ms").value_or("30000")));
    } catch (...) {
      std::cout << json{{"ok", false}, {"error", "invalid_timeout_ms"}, {"value", argValue(pos, "--timeout-ms").value_or("")}}.dump(2) << std::endl;
      return 1;
    }
    const auto remoteTask = httpPostJson(base + "/api/tasks", json{{"channel", "cli"},
                                                                     {"peerId", "agent:" + id},
                                                                     {"text", *message},
                                                                     {"timeoutMs", timeoutMs}}, authHeaderFromEnv(cfg));
    if (remoteTask.has_value() && remoteTask->value("ok", false)) {
      json out = *remoteTask;
      out["agent"] = *agent;
      out["mode"] = "gateway-task";
      std::cout << out.dump(2) << std::endl;
      return 0;
    }

    const std::string sessionKey = agent->value("sessionKey", id == "main" ? "main" : "agent:" + id);
    const auto remoteMsg = httpPostJson(base + "/api/message", json{{"sessionKey", sessionKey}, {"text", *message}}, authHeaderFromEnv(cfg));
    if (remoteMsg.has_value() && remoteMsg->value("ok", false)) {
      json out = *remoteMsg;
      out["agent"] = *agent;
      out["mode"] = "gateway-message";
      std::cout << out.dump(2) << std::endl;
      return 0;
    }

    clawforge::session::SessionStore sessions(cfg.stateDir);
    sessions.init();
    sessions.ensureSession(sessionKey);
    sessions.appendMessage(sessionKey, "user", *message);
    std::cout << json{{"ok", true},
                      {"mode", "local-session"},
                      {"queued", false},
                      {"agent", *agent},
                      {"sessionKey", sessionKey},
                      {"note", "Gateway unavailable: stored as user message only"}}
                     .dump(2)
              << std::endl;
    return 0;
  }

  return printNotImplJson(top, sub, available);
}
int runSessions(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath); const auto remote = httpGetJson("http://" + cfg.http.host + ":" + std::to_string(cfg.http.port) + "/api/sessions", authHeaderFromEnv(cfg));
  if (remote.has_value() && remote->value("ok", false)) { std::cout << remote->dump(2) << std::endl; return 0; }
  clawforge::session::SessionStore sessions(cfg.stateDir); sessions.init(); json arr = json::array(); for (const auto& s : sessions.listSessions()) arr.push_back({{"key", s.key}, {"sessionId", s.sessionId}, {"updatedAt", s.updatedAt}});
  std::cout << json({{"ok", true}, {"mode", "local"}, {"sessions", arr}}).dump(2) << std::endl; return 0;
}

json cronJobToJson(const clawforge::automation::CronJob& job) {
  return {{"id", job.id},
          {"name", job.name},
          {"description", job.description},
          {"kind", job.kind},
          {"everyMs", job.everyMs},
          {"at", job.atIso},
          {"cron", job.cronExpr},
          {"schedule", {{"kind", job.kind}, {"everyMs", job.everyMs}, {"at", job.atIso}, {"expr", job.cronExpr}, {"tz", job.tz}}},
          {"nextRunAt", job.nextRunAt},
          {"sessionKey", job.sessionKey},
          {"message", job.message},
          {"sessionTarget", job.sessionTarget},
          {"wakeMode", job.wakeMode},
          {"agentId", job.agentId},
          {"deleteAfterRun", job.deleteAfterRun},
          {"payload", {{"kind", job.payload.kind}, {"text", job.payload.text}, {"message", job.payload.text}, {"model", job.payload.model}, {"thinking", job.payload.thinking}, {"timeoutSeconds", job.payload.timeoutSeconds}}},
          {"delivery", {{"mode", job.delivery.mode}, {"channel", job.delivery.channel}, {"to", job.delivery.to}, {"bestEffort", job.delivery.bestEffort}}},
          {"enabled", job.enabled},
          {"consecutiveErrors", job.consecutiveErrors},
          {"lastRunAt", job.lastRunAt},
          {"lastSuccessAt", job.lastSuccessAt}};
}

int runCronList(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const auto remote = httpGetJson("http://" + cfg.http.host + ":" + std::to_string(cfg.http.port) + "/api/cron/jobs", authHeaderFromEnv(cfg));
  if (remote.has_value() && remote->value("ok", false)) {
    std::cout << remote->dump(2) << std::endl;
    return 0;
  }

  clawforge::core::EventBus bus;
  clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
  if (!cron.init()) return 1;

  json arr = json::array();
  for (const auto& job : cron.listJobs()) arr.push_back(cronJobToJson(job));
  std::cout << json({{"ok", true}, {"mode", "local"}, {"jobs", arr}}).dump(2) << std::endl;
  return 0;
}

int runCronGet(const std::string& configPath, const std::string& id) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const auto remote = httpGetJson("http://" + cfg.http.host + ":" + std::to_string(cfg.http.port) + "/api/cron/jobs/" + id,
                                  authHeaderFromEnv(cfg));
  if (remote.has_value() && remote->value("ok", false)) {
    std::cout << remote->dump(2) << std::endl;
    return 0;
  }

  clawforge::core::EventBus bus;
  clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
  if (!cron.init()) return 1;
  const auto out = cron.getJob(id);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}
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
                         {"refreshed", auth.refreshed},
                         {"warnings", auth.warnings}});
  }
  std::cout << json({{"ok", true}, {"current", cfg.modelRouting.current}, {"providers", providers}}).dump(2) << std::endl; return 0;
}

int runModelsAuth(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  clawforge::models::AuthProfileStore store(cfg.stateDir);
  if (!store.init()) {
    std::cerr << "Cannot init auth profile store" << std::endl;
    return 1;
  }

  if (pos.size() >= 3 && pos[2] == "list") {
    json arr = json::array();
    for (const auto& p : store.list()) {
      arr.push_back({{"id", p.id},
                     {"provider", p.provider},
                     {"mode", p.mode},
                     {"expiresAt", p.expiresAt},
                     {"expired", clawforge::models::AuthProfileStore::isExpired(p.expiresAt)},
                     {"meta", p.meta},
                     {"token", "***"}});
    }
    std::cout << json{{"ok", true}, {"profiles", arr}}.dump(2) << std::endl;
    return 0;
  }

  if (pos.size() >= 3 && pos[2] == "order") {
    const auto provider = argValue(pos, "--provider");
    if (!provider.has_value()) {
      std::cerr << "Usage: models auth order <get|set|clear> --provider <name> [--profile-id <id> ...]" << std::endl;
      return 1;
    }
    if (pos.size() < 4) {
      std::cerr << "Usage: models auth order <get|set|clear> --provider <name>" << std::endl;
      return 1;
    }

    const std::string action = pos[3];
    if (action == "get") {
      std::cout << json{{"ok", true},
                        {"provider", *provider},
                        {"activeProfileId", store.activeProfileId(*provider).value_or("")},
                        {"order", store.orderForProvider(*provider)}}
                       .dump(2)
                << std::endl;
      return 0;
    }

    if (action == "set") {
      std::vector<std::string> ids;
      for (size_t i = 0; i < pos.size(); ++i) {
        if (pos[i] == "--profile-id" && i + 1 < pos.size()) {
          ids.push_back(pos[i + 1]);
          ++i;
          continue;
        }
        if (pos[i] == "--profiles" && i + 1 < pos.size()) {
          std::string csv = pos[i + 1];
          std::string cur;
          for (char ch : csv) {
            if (ch == ',') {
              if (!cur.empty()) ids.push_back(cur);
              cur.clear();
            } else {
              cur.push_back(ch);
            }
          }
          if (!cur.empty()) ids.push_back(cur);
          ++i;
        }
      }

      if (ids.empty()) {
        std::cerr << "Usage: models auth order set --provider <name> --profile-id <id> [--profile-id <id2>...]" << std::endl;
        return 1;
      }

      if (!store.setOrderForProvider(*provider, ids)) {
        std::cerr << "Cannot set auth order (unknown profile id or provider mismatch)" << std::endl;
        return 1;
      }

      std::cout << json{{"ok", true}, {"provider", *provider}, {"order", store.orderForProvider(*provider)}}.dump(2)
                << std::endl;
      return 0;
    }

    if (action == "clear") {
      if (!store.clearOrderForProvider(*provider)) {
        std::cerr << "Cannot clear auth order" << std::endl;
        return 1;
      }
      std::cout << json{{"ok", true}, {"provider", *provider}, {"order", json::array()}}.dump(2) << std::endl;
      return 0;
    }

    std::cerr << "Unknown models auth order action" << std::endl;
    return 1;
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
    if (!envVal || std::string(envVal).empty()) {
      std::cerr << "Env is empty or missing: " << envName << std::endl;
      return 1;
    }
    clawforge::models::AuthProfile p;
    p.id = *profileId;
    p.provider = *provider;
    p.mode = apiKeyEnv.has_value() ? "api_key" : "oauth_token";
    p.token = envVal;
    p.meta = json{{"sourceEnv", envName}};
    if (!store.upsert(p)) return 1;
    std::cout << json{{"ok", true}, {"profileId", p.id}, {"provider", p.provider}, {"mode", p.mode}}.dump(2)
              << std::endl;
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
    p.id = *profileId;
    p.provider = *provider;
    p.mode = "oauth_token";
    p.token = *token;
    p.meta = json{{"setup", "paste-token"}};
    if (expiresIn.has_value()) p.expiresAt = clawforge::models::AuthProfileStore::addSecondsUtcRfc3339(std::stoi(*expiresIn));
    if (!store.upsert(p)) return 1;
    std::cout << json{{"ok", true},
                      {"profileId", p.id},
                      {"provider", p.provider},
                      {"mode", p.mode},
                      {"expiresAt", p.expiresAt}}
                     .dump(2)
              << std::endl;
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
      std::cout << "Paste OAuth token for openai-codex: ";
      std::string in;
      std::getline(std::cin, in);
      token = in;
    }
    if (!token.has_value() || token->empty()) {
      std::cerr << "No token provided." << std::endl;
      return 1;
    }
    clawforge::models::AuthProfile p;
    p.id = id;
    p.provider = *provider;
    p.mode = "oauth_token";
    p.token = *token;
    p.meta = json{{"setup", "setup-token"},
                  {"note", "Token was provided manually. Prefer `models auth login --provider openai-codex` for OAuth flow."}};
    if (expiresIn.has_value()) p.expiresAt = clawforge::models::AuthProfileStore::addSecondsUtcRfc3339(std::stoi(*expiresIn));
    if (!store.upsert(p)) return 1;
    std::cout << json{{"ok", true},
                      {"provider", p.provider},
                      {"profileId", p.id},
                      {"mode", p.mode},
                      {"expiresAt", p.expiresAt}}
                     .dump(2)
              << std::endl;
    return 0;
  }

  if (pos.size() >= 3 && pos[2] == "login") {
    const auto provider = argValue(pos, "--provider");
    const auto profileId = argValue(pos, "--profile-id");
    const auto sourceProfileId = argValue(pos, "--source-profile-id");
    const auto importPathArg = argValue(pos, "--openclaw-auth-file");
    const auto deviceCodeJsonArg = argValue(pos, "--device-code-json");
    const auto tokenUrlArg = argValue(pos, "--token-url");
    const auto clientIdArg = argValue(pos, "--client-id");
    const auto scopeArg = argValue(pos, "--scope");
    const auto deviceStartUrlArg = argValue(pos, "--device-start-url");
    const bool poll = hasFlag(pos, "--poll");
    const bool forceBridge = hasFlag(pos, "--bridge-import");
    const bool legacySkipLogin = hasFlag(pos, "--skip-login");

    if (!provider.has_value()) {
      std::cerr << "Usage: models auth login --provider <name> [--profile-id <dest-id>] [native: --device-code-json <json|@file> [--poll] [--token-url <url>] [--client-id <id>] [--scope <scope>] [--device-start-url <url>]] [bridge: --bridge-import|--skip-login|--openclaw-auth-file <path> [--source-profile-id <id>]]" << std::endl;
      return 1;
    }

    if (*provider != "openai-codex") {
      std::cerr << "models auth login baseline currently supports --provider openai-codex" << std::endl;
      return 1;
    }

    const bool bridgeMode = forceBridge || legacySkipLogin || importPathArg.has_value() || sourceProfileId.has_value();

    if (!bridgeMode) {
      std::string clientId;
      if (clientIdArg.has_value()) {
        clientId = *clientIdArg;
      } else if (const char* envClient = std::getenv("OPENAI_CODEX_CLIENT_ID"); envClient && std::string(envClient).size() > 0) {
        clientId = envClient;
      }

      std::string scope;
      if (scopeArg.has_value()) {
        scope = *scopeArg;
      } else if (const char* envScope = std::getenv("OPENAI_CODEX_SCOPE"); envScope && std::string(envScope).size() > 0) {
        scope = envScope;
      }

      const std::string deviceStartUrl =
          deviceStartUrlArg.value_or(std::getenv("OPENAI_CODEX_DEVICE_START_URL") ? std::string(std::getenv("OPENAI_CODEX_DEVICE_START_URL"))
                                                                                    : std::string("https://api.openai.com/v1/oauth/device/code"));

      std::string deviceCodeRaw;
      if (deviceCodeJsonArg.has_value()) {
        deviceCodeRaw = *deviceCodeJsonArg;
        if (!deviceCodeRaw.empty() && deviceCodeRaw[0] == '@') {
          auto maybe = clawforge::util::FileUtil::readText(deviceCodeRaw.substr(1));
          if (!maybe.has_value()) {
            std::cout << json{{"ok", false},
                              {"provider", *provider},
                              {"native", true},
                              {"error", "Cannot read device-code JSON file"},
                              {"path", deviceCodeRaw.substr(1)}}
                             .dump(2)
                      << std::endl;
            return 1;
          }
          deviceCodeRaw = *maybe;
        }
      } else {
        if (clientId.empty()) {
          std::cout << json{{"ok", false},
                            {"provider", *provider},
                            {"native", true},
                            {"phase", "device_code_start"},
                            {"error", "client_id is required for device-code start (pass --client-id or set OPENAI_CODEX_CLIENT_ID)"}}
                           .dump(2)
                    << std::endl;
          return 1;
        }

        std::string startCmd = "curl -sS --max-time 10 -w '\n__HTTP__%{http_code}' -X POST " + clawforge::util::Shell::quote(deviceStartUrl) +
                               " -H " + clawforge::util::Shell::quote("Content-Type: application/x-www-form-urlencoded") +
                               " --data-urlencode " + clawforge::util::Shell::quote("client_id=" + clientId);
        if (!scope.empty()) {
          startCmd += " --data-urlencode " + clawforge::util::Shell::quote("scope=" + scope);
        }

        const auto startRes = clawforge::util::Shell::run(startCmd);
        if (startRes.exitCode != 0) {
          std::cout << json{{"ok", false},
                            {"provider", *provider},
                            {"native", true},
                            {"phase", "device_code_start"},
                            {"error", "OAuth device-code start request failed"},
                            {"deviceStartUrl", deviceStartUrl},
                            {"details", startRes.output}}
                           .dump(2)
                    << std::endl;
          return 1;
        }

        const std::string marker = "\n__HTTP__";
        const auto markerPos = startRes.output.rfind(marker);
        if (markerPos == std::string::npos) {
          std::cout << json{{"ok", false},
                            {"provider", *provider},
                            {"native", true},
                            {"phase", "device_code_start"},
                            {"error", "Unexpected OAuth device-code start response format"},
                            {"deviceStartUrl", deviceStartUrl}}
                           .dump(2)
                    << std::endl;
          return 1;
        }

        const std::string startBody = startRes.output.substr(0, markerPos);
        const std::string startCodeRaw = startRes.output.substr(markerPos + marker.size());
        int startHttpCode = 0;
        try {
          startHttpCode = std::stoi(startCodeRaw);
        } catch (...) {
          startHttpCode = 0;
        }

        auto startJson = json::parse(startBody, nullptr, false);
        if (startJson.is_discarded() || !startJson.is_object()) {
          std::cout << json{{"ok", false},
                            {"provider", *provider},
                            {"native", true},
                            {"phase", "device_code_start"},
                            {"error", "OAuth device-code start response is not valid JSON"},
                            {"deviceStartUrl", deviceStartUrl},
                            {"httpStatus", startHttpCode},
                            {"raw", startBody}}
                           .dump(2)
                    << std::endl;
          return 1;
        }

        const std::string startError = startJson.value("error", "");
        if (startHttpCode >= 400 || !startError.empty()) {
          std::cout << json{{"ok", false},
                            {"provider", *provider},
                            {"native", true},
                            {"phase", "device_code_start"},
                            {"error", startError.empty() ? std::string("oauth_device_code_start_failed") : startError},
                            {"error_description", startJson.value("error_description", "")},
                            {"deviceStartUrl", deviceStartUrl},
                            {"httpStatus", startHttpCode}}
                           .dump(2)
                    << std::endl;
          return 1;
        }

        deviceCodeRaw = startJson.dump();
      }

      auto deviceCode = json::parse(deviceCodeRaw, nullptr, false);
      if (deviceCode.is_discarded() || !deviceCode.is_object()) {
        std::cout << json{{"ok", false}, {"provider", *provider}, {"native", true}, {"error", "Invalid --device-code-json payload"}}
                         .dump(2)
                  << std::endl;
        return 1;
      }

      const std::string deviceCodeValue = deviceCode.value("device_code", "");
      const std::string userCode = deviceCode.value("user_code", "");
      std::string verificationUri = deviceCode.value("verification_uri", "");
      if (verificationUri.empty()) verificationUri = deviceCode.value("verification_url", "");
      const std::string verificationUriComplete = deviceCode.value("verification_uri_complete", "");
      const int interval = deviceCode.value("interval", 5);
      const int expiresIn = deviceCode.value("expires_in", 900);

      if (deviceCodeValue.empty()) {
        std::cout << json{{"ok", false}, {"provider", *provider}, {"native", true}, {"error", "device_code is required in device-code payload"}}
                         .dump(2)
                  << std::endl;
        return 1;
      }

      if (!poll) {
        std::cout << json{{"ok", true},
                          {"provider", *provider},
                          {"native", true},
                          {"phase", "device_code_ready"},
                          {"deviceStartUrl", deviceStartUrl},
                          {"deviceCode", json{{"device_code", deviceCodeValue},
                                               {"user_code", userCode},
                                               {"verification_uri", verificationUri},
                                               {"verification_uri_complete", verificationUriComplete},
                                               {"interval", interval},
                                               {"expires_in", expiresIn}}},
                          {"next", "Authorize with verification_uri + user_code, then run the same command with --poll (or pass --device-code-json explicitly)."}}
                         .dump(2)
                  << std::endl;
        return 0;
      }

      const std::string tokenUrl = tokenUrlArg.value_or("https://api.openai.com/v1/oauth/token");
      if (clientId.empty()) {
        std::cout << json{{"ok", false},
                          {"provider", *provider},
                          {"native", true},
                          {"poll", true},
                          {"error", "client_id is required for --poll (pass --client-id or set OPENAI_CODEX_CLIENT_ID)"}}
                         .dump(2)
                  << std::endl;
        return 1;
      }

      const std::string grantType = "urn:ietf:params:oauth:grant-type:device_code";
      std::string cmd = "curl -sS --max-time 10 -w '\n__HTTP__%{http_code}' -X POST " + clawforge::util::Shell::quote(tokenUrl) +
                        " -H " + clawforge::util::Shell::quote("Content-Type: application/x-www-form-urlencoded") +
                        " --data-urlencode " + clawforge::util::Shell::quote("grant_type=" + grantType) +
                        " --data-urlencode " + clawforge::util::Shell::quote("device_code=" + deviceCodeValue) +
                        " --data-urlencode " + clawforge::util::Shell::quote("client_id=" + clientId);

      const auto pollRes = clawforge::util::Shell::run(cmd);
      if (pollRes.exitCode != 0) {
        std::cout << json{{"ok", false},
                          {"provider", *provider},
                          {"native", true},
                          {"poll", true},
                          {"error", "OAuth token poll request failed"},
                          {"details", pollRes.output}}
                         .dump(2)
                  << std::endl;
        return 1;
      }

      const std::string marker = "\n__HTTP__";
      const auto markerPos = pollRes.output.rfind(marker);
      if (markerPos == std::string::npos) {
        std::cout << json{{"ok", false},
                          {"provider", *provider},
                          {"native", true},
                          {"poll", true},
                          {"error", "Unexpected OAuth poll response format"}}
                         .dump(2)
                  << std::endl;
        return 1;
      }

      const std::string body = pollRes.output.substr(0, markerPos);
      const std::string httpCodeRaw = pollRes.output.substr(markerPos + marker.size());
      int httpCode = 0;
      try {
        httpCode = std::stoi(httpCodeRaw);
      } catch (...) {
        httpCode = 0;
      }

      auto tokenResp = json::parse(body, nullptr, false);
      if (tokenResp.is_discarded() || !tokenResp.is_object()) {
        std::cout << json{{"ok", false},
                          {"provider", *provider},
                          {"native", true},
                          {"poll", true},
                          {"error", "OAuth poll response is not valid JSON"},
                          {"httpStatus", httpCode},
                          {"raw", body}}
                         .dump(2)
                  << std::endl;
        return 1;
      }

      const std::string oauthError = tokenResp.value("error", "");
      if (httpCode >= 400 || !oauthError.empty()) {
        const bool retryable = (oauthError == "authorization_pending" || oauthError == "slow_down");
        std::cout << json{{"ok", false},
                          {"provider", *provider},
                          {"native", true},
                          {"poll", true},
                          {"retryable", retryable},
                          {"error", oauthError.empty() ? std::string("oauth_poll_failed") : oauthError},
                          {"error_description", tokenResp.value("error_description", "")},
                          {"httpStatus", httpCode},
                          {"interval", interval}}
                         .dump(2)
                  << std::endl;
        return retryable ? 2 : 1;
      }

      const std::string accessToken = tokenResp.value("access_token", "");
      if (accessToken.empty()) {
        std::cout << json{{"ok", false},
                          {"provider", *provider},
                          {"native", true},
                          {"poll", true},
                          {"error", "OAuth poll succeeded but access_token is missing"},
                          {"httpStatus", httpCode}}
                         .dump(2)
                  << std::endl;
        return 1;
      }

      clawforge::models::AuthProfile p;
      p.id = profileId.value_or(std::string("openai-codex-default"));
      p.provider = *provider;
      p.mode = "oauth_token";
      p.token = accessToken;
      const int tokenExpiresIn = tokenResp.value("expires_in", 0);
      if (tokenExpiresIn > 0) p.expiresAt = clawforge::models::AuthProfileStore::addSecondsUtcRfc3339(tokenExpiresIn);
      p.meta = json{{"nativeDeviceCode", true},
                    {"tokenUrl", tokenUrl},
                    {"deviceStartUrl", deviceStartUrl},
                    {"clientId", clientId},
                    {"scope", tokenResp.value("scope", scope)},
                    {"tokenType", tokenResp.value("token_type", "")}};

      if (!store.upsert(p)) {
        std::cout << json{{"ok", false}, {"error", "Cannot store OAuth auth profile"}}.dump(2) << std::endl;
        return 1;
      }
      (void)store.setActive(*provider, p.id);

      std::cout << json{{"ok", true},
                        {"provider", p.provider},
                        {"profileId", p.id},
                        {"mode", p.mode},
                        {"expiresAt", p.expiresAt},
                        {"native", true},
                        {"polled", true}}
                       .dump(2)
                << std::endl;
      return 0;
    }

    const bool skipLogin = legacySkipLogin || importPathArg.has_value();
    if (!skipLogin) {
      std::string cmd = "openclaw models auth login --provider " + clawforge::util::Shell::quote(*provider);
      if (profileId.has_value()) cmd += " --profile-id " + clawforge::util::Shell::quote(*profileId);
      const auto loginRes = clawforge::util::Shell::run(cmd);
      if (loginRes.exitCode != 0) {
        std::cout << json{{"ok", false},
                          {"error", "openclaw OAuth login failed"},
                          {"provider", *provider},
                          {"details", loginRes.output}}
                         .dump(2)
                  << std::endl;
        return 1;
      }
    }

    std::filesystem::path openclawAuthPath;
    if (importPathArg.has_value() && !importPathArg->empty()) {
      openclawAuthPath = *importPathArg;
    } else if (const char* stateDir = std::getenv("OPENCLAW_STATE_DIR"); stateDir && std::string(stateDir).size() > 0) {
      openclawAuthPath = std::filesystem::path(stateDir) / "agents" / "main" / "agent" / "auth-profiles.json";
    } else if (const char* home = std::getenv("HOME"); home && std::string(home).size() > 0) {
      openclawAuthPath = std::filesystem::path(home) / ".openclaw" / "agents" / "main" / "agent" / "auth-profiles.json";
    }

    auto raw = clawforge::util::FileUtil::readText(openclawAuthPath);
    if (!raw.has_value()) {
      std::cout << json{{"ok", false},
                        {"error", "Cannot read OpenClaw auth profile store for import"},
                        {"path", openclawAuthPath.string()}}
                       .dump(2)
                << std::endl;
      return 1;
    }

    auto parsed = json::parse(*raw, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
      std::cout << json{{"ok", false}, {"error", "OpenClaw auth store JSON is invalid"}}.dump(2) << std::endl;
      return 1;
    }

    struct Imported {
      std::string key;
      std::string profileId;
      std::string token;
      std::string expiresAt;
      std::string accountId;
    };

    std::vector<Imported> imported;

    const auto readOne = [&](const std::string& key, const json& v) {
      if (!v.is_object()) return;
      std::string prov = v.value("provider", "");
      if (prov.empty()) {
        const auto colon = key.find(':');
        prov = (colon == std::string::npos) ? key : key.substr(0, colon);
      }
      if (prov != *provider) return;

      const std::string token = v.value("access", v.value("token", ""));
      if (token.empty()) return;

      Imported item;
      item.key = key;
      item.token = token;
      item.accountId = v.value("accountId", "");
      item.profileId = v.value("profileId", "");
      if (item.profileId.empty()) {
        const auto colon = key.find(':');
        if (colon != std::string::npos && colon + 1 < key.size()) item.profileId = key.substr(colon + 1);
      }
      if (v.contains("expires") && v["expires"].is_string()) item.expiresAt = v["expires"].get<std::string>();
      imported.push_back(std::move(item));
    };

    if (parsed.contains("profiles")) {
      const auto& profiles = parsed["profiles"];
      if (profiles.is_object()) {
        for (auto it = profiles.begin(); it != profiles.end(); ++it) readOne(it.key(), it.value());
      } else if (profiles.is_array()) {
        for (const auto& v : profiles) {
          if (!v.is_object()) continue;
          readOne(v.value("id", ""), v);
        }
      }
    } else {
      for (auto it = parsed.begin(); it != parsed.end(); ++it) readOne(it.key(), it.value());
    }

    if (imported.empty()) {
      std::cout << json{{"ok", false},
                        {"error", "OAuth login succeeded, but no importable token found in OpenClaw store"},
                        {"path", openclawAuthPath.string()}}
                       .dump(2)
                << std::endl;
      return 1;
    }

    Imported chosen = imported.front();
    if (sourceProfileId.has_value()) {
      bool found = false;
      for (const auto& it : imported) {
        if (it.profileId == *sourceProfileId || it.key == *sourceProfileId ||
            it.key == (*provider + ":" + *sourceProfileId)) {
          chosen = it;
          found = true;
          break;
        }
      }
      if (!found) {
        std::cout << json{{"ok", false},
                          {"error", "Requested source-profile-id not found in imported OpenClaw auth data"},
                          {"requested", *sourceProfileId}}
                         .dump(2)
                  << std::endl;
        return 1;
      }
    }

    clawforge::models::AuthProfile p;
    p.id = profileId.value_or(chosen.profileId.empty() ? std::string("openai-codex-default") : chosen.profileId);
    p.provider = *provider;
    p.mode = "oauth_token";
    p.token = chosen.token;
    p.expiresAt = chosen.expiresAt;
    p.meta = json{{"importedFrom", "openclaw models auth login"},
                  {"sourcePath", openclawAuthPath.string()},
                  {"openclawProfile", chosen.key},
                  {"sourceProfileId", chosen.profileId},
                  {"accountId", chosen.accountId},
                  {"skipLogin", skipLogin}};

    if (!store.upsert(p)) {
      std::cout << json{{"ok", false}, {"error", "Cannot store imported auth profile"}}.dump(2) << std::endl;
      return 1;
    }
    (void)store.setActive(*provider, p.id);

    std::cout << json{{"ok", true},
                      {"provider", p.provider},
                      {"profileId", p.id},
                      {"mode", p.mode},
                      {"expiresAt", p.expiresAt},
                      {"imported", true},
                      {"path", openclawAuthPath.string()},
                      {"bridge", true}}
                     .dump(2)
              << std::endl;
    return 0;
  }


  if (pos.size() >= 3 && pos[2] == "use") {
    const auto provider = argValue(pos, "--provider");
    const auto profileId = argValue(pos, "--profile-id");
    if (!provider.has_value() || !profileId.has_value()) {
      std::cerr << "Usage: models auth use --provider <name> --profile-id <id>" << std::endl;
      return 1;
    }
    if (!store.setActive(*provider, *profileId)) {
      std::cerr << "Cannot set active profile (not found or provider mismatch)" << std::endl;
      return 1;
    }
    std::cout << json{{"ok", true}, {"provider", *provider}, {"activeProfileId", *profileId}}.dump(2) << std::endl;
    return 0;
  }

  if (pos.size() >= 3 && pos[2] == "remove") {
    const auto profileId = argValue(pos, "--profile-id");
    if (!profileId.has_value()) {
      std::cerr << "Usage: models auth remove --profile-id <id>" << std::endl;
      return 1;
    }
    if (!store.remove(*profileId)) {
      std::cerr << "Profile not found: " << *profileId << std::endl;
      return 1;
    }
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

json defaultNodesRegistry() {
  return json{{"nodes", json::array({json{{"id", "local-node"},
                                           {"name", "Local Runtime Node"},
                                           {"platform", "darwin"},
                                           {"connected", true},
                                           {"capabilities", json::array({"status", "describe", "invoke:read-safe", "runtime:probe", "canvas:status", "canvas:snapshot"})}}})}};
}

json loadNodesRegistry(const clawforge::core::AppConfig& cfg) {
  const auto path = cfg.stateDir / "nodes" / "registry.json";
  if (std::filesystem::exists(path)) {
    auto raw = clawforge::util::FileUtil::readText(path.string()).value_or("{}");
    auto parsed = json::parse(raw, nullptr, false);
    if (!parsed.is_discarded() && parsed.is_object() && parsed.contains("nodes") && parsed["nodes"].is_array()) return parsed;
  }
  return defaultNodesRegistry();
}

std::optional<json> findNodeById(const json& nodes, const std::string& id) {
  for (const auto& n : nodes) if (n.value("id", "") == id) return n;
  return std::nullopt;
}

bool nodeRuntimeAvailable(const json& node) {
  return node.value("connected", false) && node.value("id", "") == "local-node";
}

json localRuntimeProbe() {
  const auto uname = clawforge::util::Shell::run("uname -sm");
  const auto whoami = clawforge::util::Shell::run("whoami");
  const auto host = clawforge::util::Shell::run("hostname");
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  return json{{"timestampSec", now},
              {"host", host.exitCode == 0 ? host.output : ""},
              {"user", whoami.exitCode == 0 ? whoami.output : ""},
              {"platform", uname.exitCode == 0 ? uname.output : "unknown"},
              {"cwd", std::filesystem::current_path().string()},
              {"mode", "local-read-safe"}};
}

json nodeRuntimeUnavailable(const std::string& method, const std::string& node, const std::string& action, const std::string& reason) {
  return json{{"ok", false},
              {"method", method},
              {"node", node},
              {"action", action},
              {"error", "node_runtime_unavailable"},
              {"reason", reason},
              {"readSafeOnly", true},
              {"capabilityGate", json{{"feature", "node.runtime"}, {"available", false}}}};
}

json devicesFromRegistry(const json& reg) {
  json devices = json::array();
  for (const auto& n : reg.value("nodes", json::array())) {
    devices.push_back(json{{"id", n.value("id", "")},
                           {"name", n.value("name", "")},
                           {"online", n.value("connected", false)},
                           {"type", "paired-node"},
                           {"runtime", json{{"available", nodeRuntimeAvailable(n)}, {"mode", nodeRuntimeAvailable(n) ? "local-read-safe" : "unavailable"}}}});
  }
  return devices;
}

json devicesMethod(const clawforge::core::AppConfig& cfg, const std::string& method, const json& params) {
  const auto reg = loadNodesRegistry(cfg);
  const auto devices = devicesFromRegistry(reg);

  if (method == "devices.list") {
    return json{{"ok", true}, {"method", method}, {"devices", devices}, {"count", devices.size()}, {"baseline", false}, {"stage", "stage25-slice2"}};
  }

  if (method == "devices.status") {
    size_t online = 0;
    for (const auto& d : devices) if (d.value("online", false)) ++online;
    return json{{"ok", true},
                {"method", method},
                {"status", json{{"total", devices.size()}, {"online", online}, {"offline", devices.size() - online}}},
                {"baseline", false},
                {"stage", "stage25-slice2"}};
  }

  if (method == "devices.invoke") {
    const std::string id = params.value("device", params.value("id", std::string("local-node")));
    const std::string action = params.value("action", std::string("status"));
    const auto node = findNodeById(reg.value("nodes", json::array()), id);
    if (!node.has_value()) return json{{"ok", false}, {"method", method}, {"error", "device_not_found"}, {"device", id}};
    if (!nodeRuntimeAvailable(*node)) return json{{"ok", false},
                                                  {"method", method},
                                                  {"device", id},
                                                  {"action", action},
                                                  {"error", "device_runtime_unavailable"},
                                                  {"readSafeOnly", true}};
    if (action == "status" || action == "probe" || action == "metrics") {
      return json{{"ok", true},
                  {"method", method},
                  {"device", id},
                  {"action", action},
                  {"runtime", localRuntimeProbe()},
                  {"readSafeOnly", true},
                  {"stage", "stage25-slice2"}};
    }
    return json{{"ok", false},
                {"method", method},
                {"device", id},
                {"action", action},
                {"error", "device_action_not_allowed"},
                {"readSafeOnly", true},
                {"allowedActions", json::array({"status", "probe", "metrics"})}};
  }

  return json{{"ok", false}, {"method", method}, {"error", "unsupported_devices_method"}};
}

json canvasMethod(const clawforge::core::AppConfig& cfg, const std::string& method, const json& params) {
  const auto reg = loadNodesRegistry(cfg);
  const auto node = findNodeById(reg.value("nodes", json::array()), params.value("node", std::string("local-node")));
  const bool available = node.has_value() && nodeRuntimeAvailable(*node);

  if (method == "canvas.status") {
    return json{{"ok", true},
                {"method", method},
                {"canvas", json{{"available", available},
                                 {"mode", available ? "local-read-safe" : "baseline-stub"},
                                 {"reason", available ? "runtime_ready" : "runtime_not_configured"},
                                 {"supported", json::array({"status", "list", "snapshot", "invoke"})},
                                 {"invokeActions", json::array({"present", "hide", "navigate", "snapshot"})}}},
                {"readSafe", true},
                {"stage", "stage25-slice2"}};
  }

  if (method == "canvas.snapshot") {
    if (!available) {
      return json{{"ok", false}, {"method", method}, {"error", "canvas_runtime_unavailable"}, {"readSafeOnly", true}, {"hint", "Connect local-node runtime"}};
    }
    return json{{"ok", true},
                {"method", method},
                {"snapshot", json{{"kind", "virtual"}, {"node", "local-node"}, {"runtime", localRuntimeProbe()}, {"contentHash", hashText(localRuntimeProbe().dump())}}},
                {"readSafe", true},
                {"stage", "stage25-slice2"}};
  }

  if (method == "canvas.invoke") {
    const std::string action = params.value("action", std::string("snapshot"));
    if (!available) return json{{"ok", false}, {"method", method}, {"action", action}, {"error", "canvas_runtime_unavailable"}, {"readSafeOnly", true}};
    if (action == "snapshot" || action == "status") return canvasMethod(cfg, "canvas.snapshot", params);
    if (action == "present" || action == "hide" || action == "navigate") {
      return json{{"ok", true},
                  {"method", method},
                  {"action", action},
                  {"invoke", json{{"executed", true}, {"readSafe", true}, {"note", "modeled-runtime-action"}}},
                  {"runtime", localRuntimeProbe()},
                  {"stage", "stage25-slice2"}};
    }
    return json{{"ok", false},
                {"method", method},
                {"action", action},
                {"error", "canvas_action_not_allowed"},
                {"readSafeOnly", true},
                {"allowedActions", json::array({"present", "hide", "navigate", "snapshot", "status"})}};
  }

  return json{{"ok", false}, {"method", method}, {"error", "unsupported_canvas_method"}};
}

json nodesMethod(const clawforge::core::AppConfig& cfg, const std::string& method, const json& params) {
  const auto reg = loadNodesRegistry(cfg);
  auto nodes = reg.value("nodes", json::array());

  for (auto& n : nodes) {
    n["runtime"] = json{{"available", nodeRuntimeAvailable(n)},
                         {"mode", nodeRuntimeAvailable(n) ? "local-read-safe" : "unavailable"},
                         {"allowedActions", json::array({"status", "describe", "probe", "health"})}};
  }

  if (method == "nodes.list") {
    return json{{"ok", true}, {"method", method}, {"nodes", nodes}, {"count", nodes.size()}, {"baseline", false}, {"stage", "stage25-slice2"}};
  }

  if (method == "nodes.status") {
    size_t connected = 0;
    size_t runtimeReady = 0;
    for (const auto& n : nodes) {
      if (n.value("connected", false)) ++connected;
      if (n.contains("runtime") && n["runtime"].value("available", false)) ++runtimeReady;
    }
    return json{{"ok", true},
                {"method", method},
                {"status", json{{"total", nodes.size()}, {"connected", connected}, {"disconnected", nodes.size() - connected}, {"runtimeReady", runtimeReady}}},
                {"baseline", false},
                {"stage", "stage25-slice2"}};
  }

  if (method == "nodes.describe") {
    const std::string id = params.value("node", params.value("id", std::string("local-node")));
    for (const auto& n : nodes) {
      if (n.value("id", "") == id) return json{{"ok", true}, {"method", method}, {"node", n}, {"baseline", false}, {"stage", "stage25-slice2"}};
    }
    return json{{"ok", false}, {"method", method}, {"error", "node_not_found"}, {"node", id}};
  }

  if (method == "nodes.invoke") {
    const std::string id = params.value("node", params.value("id", std::string("local-node")));
    const std::string action = params.value("action", std::string("status"));
    const auto node = findNodeById(reg.value("nodes", json::array()), id);
    if (!node.has_value()) return json{{"ok", false}, {"method", method}, {"error", "node_not_found"}, {"node", id}};
    if (!nodeRuntimeAvailable(*node)) return nodeRuntimeUnavailable(method, id, action, "node disconnected or non-local runtime");

    if (action == "status" || action == "describe" || action == "probe" || action == "health") {
      return json{{"ok", true},
                  {"method", method},
                  {"node", id},
                  {"action", action},
                  {"invoke", json{{"readSafe", true}, {"executed", true}, {"result", "runtime-probe"}, {"runtime", localRuntimeProbe()}}},
                  {"baseline", false},
                  {"stage", "stage25-slice2"}};
    }
    return json{{"ok", false},
                {"method", method},
                {"node", id},
                {"action", action},
                {"error", "node_action_not_allowed"},
                {"readSafeOnly", true},
                {"allowedActions", json::array({"status", "describe", "probe", "health"})}};
  }

  return json{{"ok", false}, {"method", method}, {"error", "unsupported_nodes_method"}};
}

int runNodesFamily(const std::string& configPath, const std::vector<std::string>& pos, const std::string& command) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string sub = pos.size() >= 2 ? pos[1] : "list";

  if (sub == "list") {
    std::cout << nodesMethod(cfg, "nodes.list", json::object()).dump(2) << std::endl;
    return 0;
  }
  if (sub == "status") {
    std::cout << nodesMethod(cfg, "nodes.status", json::object()).dump(2) << std::endl;
    return 0;
  }
  if (sub == "describe") {
    const std::string id = argValue(pos, "--node").value_or(pos.size() >= 3 ? pos[2] : "local-node");
    const auto out = nodesMethod(cfg, "nodes.describe", json{{"node", id}});
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 1;
  }
  if (sub == "invoke") {
    const std::string id = argValue(pos, "--node").value_or("local-node");
    const std::string action = argValue(pos, "--action").value_or(pos.size() >= 3 ? pos[2] : "status");
    const auto out = nodesMethod(cfg, "nodes.invoke", json{{"node", id}, {"action", action}});
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 2;
  }

  std::cout << json{{"ok", false},
                    {"command", command},
                    {"error", "not_implemented"},
                    {"supported", json::array({"list", "status", "describe", "invoke"})}}
                   .dump(2)
            << std::endl;
  return 2;
}

int runDevicesFamily(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string sub = pos.size() >= 2 ? pos[1] : "list";

  if (sub == "list") {
    std::cout << devicesMethod(cfg, "devices.list", json::object()).dump(2) << std::endl;
    return 0;
  }
  if (sub == "status") {
    std::cout << devicesMethod(cfg, "devices.status", json::object()).dump(2) << std::endl;
    return 0;
  }
  if (sub == "invoke") {
    const std::string id = argValue(pos, "--device").value_or("local-node");
    const std::string action = argValue(pos, "--action").value_or("status");
    const auto out = devicesMethod(cfg, "devices.invoke", json{{"device", id}, {"action", action}});
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 2;
  }

  std::cout << json{{"ok", false}, {"error", "not_implemented"}, {"supported", json::array({"list", "status", "invoke"})}}.dump(2) << std::endl;
  return 2;
}

int runCanvasFamily(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string sub = pos.size() >= 2 ? pos[1] : "status";

  if (sub == "status" || sub == "list") {
    std::cout << canvasMethod(cfg, "canvas.status", json::object()).dump(2) << std::endl;
    return 0;
  }
  if (sub == "snapshot") {
    const auto out = canvasMethod(cfg, "canvas.snapshot", json::object());
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 2;
  }
  if (sub == "invoke") {
    const std::string action = argValue(pos, "--action").value_or("snapshot");
    const auto out = canvasMethod(cfg, "canvas.invoke", json{{"action", action}});
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 2;
  }
  std::cout << json{{"ok", false}, {"error", "not_implemented"}, {"supported", json::array({"status", "list", "snapshot", "invoke"})}}.dump(2)
            << std::endl;
  return 2;
}

int runBrowserStatus(const std::string& configPath) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpGetJson(baseUrl + "/api/browser/status", authHeaderFromEnv(cfg));
  if (remote.has_value()) {
    std::cout << remote->dump(2) << std::endl;
    return remote->value("ok", false) ? 0 : 1;
  }
  clawforge::browser::BrowserRelay relay(cfg.browser);
  const auto out = relay.status();
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
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

int runBrowserSnapshot(const std::string& configPath, const std::vector<std::string>& pos) {
  std::string urlHint;
  if (pos.size() >= 3 && pos[2].rfind("--", 0) != 0) urlHint = pos[2];
  const std::string targetId = argValue(pos, "--target-id").value_or("");

  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(baseUrl + "/api/browser/snapshot", json{{"url", urlHint}, {"targetId", targetId}}, authHeaderFromEnv(cfg));
  if (remote.has_value()) {
    std::cout << remote->dump(2) << std::endl;
    return remote->value("ok", false) ? 0 : 1;
  }
  clawforge::browser::BrowserRelay relay(cfg.browser);
  const auto out = relay.snapshot(urlHint, targetId);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}

int runBrowserNavigate(const std::string& configPath, const std::vector<std::string>& pos) {
  if (pos.size() < 3) {
    std::cerr << "Usage: browser navigate <url> [--target-id <id>]" << std::endl;
    return 1;
  }
  const std::string url = pos[2];
  const std::string targetId = argValue(pos, "--target-id").value_or("");

  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(baseUrl + "/api/browser/navigate", json{{"url", url}, {"targetId", targetId}}, authHeaderFromEnv(cfg));
  if (remote.has_value()) {
    std::cout << remote->dump(2) << std::endl;
    return remote->value("ok", false) ? 0 : 1;
  }
  clawforge::browser::BrowserRelay relay(cfg.browser);
  const auto out = relay.navigate(url, targetId);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}

int runBrowserClick(const std::string& configPath, const std::vector<std::string>& pos) {
  if (pos.size() < 3) {
    std::cerr << "Usage: browser click <ref> [--target-id <id>] [--double]" << std::endl;
    return 1;
  }

  const std::string ref = pos[2];
  const std::string targetId = argValue(pos, "--target-id").value_or("");
  const bool dbl = hasFlag(pos, "--double");

  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(baseUrl + "/api/browser/click",
                                   json{{"ref", ref}, {"targetId", targetId}, {"double", dbl}},
                                   authHeaderFromEnv(cfg));
  if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }

  clawforge::browser::BrowserRelay relay(cfg.browser);
  const auto out = relay.click(ref, targetId, dbl);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}

int runBrowserType(const std::string& configPath, const std::vector<std::string>& pos) {
  if (pos.size() < 4) {
    std::cerr << "Usage: browser type <ref> <text> [--target-id <id>] [--submit] [--slowly]" << std::endl;
    return 1;
  }

  const std::string ref = pos[2];
  const std::string text = pos[3];
  const std::string targetId = argValue(pos, "--target-id").value_or("");
  const bool submit = hasFlag(pos, "--submit");
  const bool slowly = hasFlag(pos, "--slowly");

  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(baseUrl + "/api/browser/type",
                                   json{{"ref", ref}, {"text", text}, {"targetId", targetId},
                                        {"submit", submit}, {"slowly", slowly}},
                                   authHeaderFromEnv(cfg));
  if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }

  clawforge::browser::BrowserRelay relay(cfg.browser);
  const auto out = relay.type(ref, text, targetId, submit, slowly);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}

int runBrowserScreenshot(const std::string& configPath, const std::vector<std::string>& pos) {
  const std::string targetId = (pos.size() >= 3 && pos[2].rfind("--", 0) != 0) ? pos[2] : "";
  const bool fullPage = hasFlag(pos, "--full-page");
  const std::string imgType = argValue(pos, "--type").value_or("png");

  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto remote = httpPostJson(baseUrl + "/api/browser/screenshot",
                                   json{{"targetId", targetId}, {"fullPage", fullPage}, {"type", imgType}},
                                   authHeaderFromEnv(cfg));
  if (remote.has_value()) { std::cout << remote->dump(2) << std::endl; return remote->value("ok", false) ? 0 : 1; }

  clawforge::browser::BrowserRelay relay(cfg.browser);
  const auto out = relay.screenshot(targetId, fullPage, imgType);
  std::cout << out.dump(2) << std::endl;
  return out.value("ok", false) ? 0 : 1;
}

int runCronAction(const std::string& configPath, const std::string& action,
                  const std::string& arg = "", const std::string& arg2 = "") {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string base = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);
  const auto auth = authHeaderFromEnv(cfg);

  if (action == "list") return runCronList(configPath);
  if (action == "get" || action == "show") return runCronGet(configPath, arg);

  if (action == "status") {
    const auto remote = httpGetJson(base + "/api/cron/status", auth);
    if (remote.has_value()) {
      std::cout << remote->dump(2) << std::endl;
      return remote->value("ok", false) ? 0 : 1;
    }
    clawforge::core::EventBus bus;
    clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
    if (!cron.init()) return 1;
    const auto out = cron.status();
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 1;
  }

  if (action == "add" || action == "validate") {
    auto payload = json::parse(arg, nullptr, false);
    if (payload.is_discarded()) {
      std::cerr << "Invalid JSON payload" << std::endl;
      return 1;
    }
    const auto remote = httpPostJson(base + (action == "add" ? "/api/cron/jobs" : "/api/cron/validate"), payload, auth);
    if (remote.has_value()) {
      std::cout << remote->dump(2) << std::endl;
      return remote->value("ok", false) ? 0 : 1;
    }
    clawforge::core::EventBus bus;
    clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
    if (!cron.init()) return 1;
    const auto out = (action == "add") ? cron.addJob(payload) : cron.validate(payload);
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 1;
  }

  if (action == "edit") {
    auto patch = json::parse(arg2, nullptr, false);
    if (patch.is_discarded()) {
      std::cerr << "Invalid JSON patch payload" << std::endl;
      return 1;
    }
    const auto remote = httpPatchJson(base + "/api/cron/jobs/" + arg, patch, auth);
    if (remote.has_value()) {
      std::cout << remote->dump(2) << std::endl;
      return remote->value("ok", false) ? 0 : 1;
    }
    clawforge::core::EventBus bus;
    clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
    if (!cron.init()) return 1;
    const auto out = cron.updateJob(arg, patch);
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 1;
  }

  if (action == "enable" || action == "disable") {
    const bool enable = action == "enable";
    const auto remote = httpPostJson(base + "/api/cron/jobs/" + arg + (enable ? "/enable" : "/disable"), json::object(), auth);
    if (remote.has_value()) {
      std::cout << remote->dump(2) << std::endl;
      return remote->value("ok", false) ? 0 : 1;
    }
    clawforge::core::EventBus bus;
    clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
    if (!cron.init()) return 1;
    const auto out = cron.setEnabled(arg, enable);
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 1;
  }

  if (action == "runs") {
    int limit = 20;
    if (!arg2.empty()) {
      try { limit = std::max(1, std::stoi(arg2)); } catch (...) { limit = 20; }
    }
    const auto remote = httpGetJson(base + "/api/cron/jobs/" + arg + "/runs?limit=" + std::to_string(limit), auth);
    if (remote.has_value()) {
      std::cout << remote->dump(2) << std::endl;
      return remote->value("ok", false) ? 0 : 1;
    }
    clawforge::core::EventBus bus;
    clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
    if (!cron.init()) return 1;
    const auto out = cron.listRuns(arg, limit);
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 1;
  }

  if (action == "run") {
    const std::string mode = arg2.empty() ? "force" : arg2;
    const auto remote = httpPostJson(base + "/api/cron/jobs/" + arg + "/run", json{{"mode", mode}}, auth);
    if (remote.has_value()) {
      std::cout << remote->dump(2) << std::endl;
      return remote->value("ok", false) ? 0 : 1;
    }
    clawforge::core::EventBus bus;
    clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
    if (!cron.init()) return 1;
    const auto out = cron.runNow(arg, mode);
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 1;
  }

  if (action == "rm" || action == "remove") {
    const auto remote = httpDeleteJson(base + "/api/cron/jobs/" + arg, auth);
    if (remote.has_value()) {
      std::cout << remote->dump(2) << std::endl;
      return remote->value("ok", false) ? 0 : 1;
    }
    clawforge::core::EventBus bus;
    clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
    if (!cron.init()) return 1;
    json out = {{"ok", cron.removeJob(arg)}, {"id", arg}};
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 1;
  }

  return 1;
}

std::vector<std::string> configuredChannels(const clawforge::core::AppConfig& cfg) {
  std::vector<std::string> out;
  if (cfg.telegram.enabled) out.push_back("telegram");
  return out;
}

bool parseTelegramTarget(const std::string& target, json& payload, std::string& error) {
  if (target.empty()) {
    error = "target is required";
    return false;
  }

  static const std::regex reUser(R"(^@[A-Za-z0-9_]{4,}$)");
  static const std::regex reChat(R"(^-?[0-9]+$)");
  static const std::regex reTopic(R"(^(-?[0-9]+):topic:([0-9]+)$)");
  static const std::regex reTopicShort(R"(^(-?[0-9]+):([0-9]+)$)");

  std::smatch m;
  if (std::regex_match(target, reUser) || std::regex_match(target, reChat)) {
    payload["chat_id"] = target;
    return true;
  }
  if (std::regex_match(target, m, reTopic) || std::regex_match(target, m, reTopicShort)) {
    payload["chat_id"] = m[1].str();
    payload["message_thread_id"] = std::stoi(m[2].str());
    return true;
  }

  error = "telegram target must be @username, chatId, or chatId:topic:threadId";
  return false;
}

json callTelegramApi(const clawforge::core::AppConfig& cfg, const std::string& method, const json& payload,
                     const std::string& action, const std::string& target,
                     bool dryRun = false) {
  if (dryRun) {
    return json{{"ok", true},
                {"dryRun", true},
                {"action", action},
                {"channel", "telegram"},
                {"target", target},
                {"payload", payload}};
  }

  const char* token = std::getenv(cfg.telegram.botTokenEnv.c_str());
  if (!token || std::string(token).empty()) {
    return json{{"ok", false}, {"error", "Telegram token env is empty"}, {"tokenEnv", cfg.telegram.botTokenEnv}};
  }

  const std::string url = "https://api.telegram.org/bot" + std::string(token) + "/" + method;
  const std::string cmd = "curl -sS -X POST " + clawforge::util::Shell::quote(url) +
                          " -H " + clawforge::util::Shell::quote("Content-Type: application/json") +
                          " --data " + clawforge::util::Shell::quote(payload.dump());
  const auto res = clawforge::util::Shell::run(cmd);
  if (res.exitCode != 0) {
    return json{{"ok", false},
                {"error", "telegram request failed"},
                {"action", action},
                {"channel", "telegram"},
                {"target", target},
                {"details", res.output}};
  }

  auto parsed = json::parse(res.output, nullptr, false);
  if (parsed.is_discarded()) {
    return json{{"ok", false}, {"error", "telegram returned non-json response"}, {"raw", res.output}};
  }

  if (!parsed.value("ok", false)) {
    return json{{"ok", false}, {"error", "telegram api error"}, {"response", parsed}};
  }

  return json{{"ok", true},
              {"action", action},
              {"channel", "telegram"},
              {"target", target},
              {"response", parsed}};
}

int runMessageSend(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);

  std::string channel = argValue(pos, "--channel").value_or("");
  const std::string target = argValue(pos, "--target").value_or("");
  const std::string message = argValue(pos, "--message").value_or("");
  const std::string media = argValue(pos, "--media").value_or("");
  const bool dryRun = hasFlag(pos, "--dry-run");

  if (target.empty()) {
    std::cout << json{{"ok", false}, {"error", "--target is required"}}.dump(2) << std::endl;
    return 1;
  }
  if (message.empty() && media.empty()) {
    std::cout << json{{"ok", false}, {"error", "--message or --media is required"}}.dump(2) << std::endl;
    return 1;
  }

  if (channel.empty()) {
    const auto channels = configuredChannels(cfg);
    if (channels.size() == 1) {
      channel = channels[0];
    } else if (channels.empty()) {
      std::cout << json{{"ok", false}, {"error", "No enabled channels configured. Pass --channel explicitly."}}.dump(2) << std::endl;
      return 1;
    } else {
      std::cout << json{{"ok", false}, {"error", "Multiple enabled channels configured. Pass --channel."}}.dump(2) << std::endl;
      return 1;
    }
  }

  if (channel != "telegram") {
    std::cout << json{{"ok", false}, {"error", "Only --channel telegram is implemented in current baseline"}, {"channel", channel}}.dump(2) << std::endl;
    return 2;
  }

  json telegramPayload;
  std::string targetError;
  if (!parseTelegramTarget(target, telegramPayload, targetError)) {
    std::cout << json{{"ok", false}, {"channel", channel}, {"target", target}, {"error", targetError}}.dump(2) << std::endl;
    return 1;
  }
  if (!message.empty()) telegramPayload["text"] = message;
  if (!media.empty()) {
    std::cout << json{{"ok", false}, {"error", "--media is not implemented yet for telegram baseline"}}.dump(2) << std::endl;
    return 2;
  }

  const auto result = callTelegramApi(cfg, "sendMessage", telegramPayload, "message.send", target, dryRun);
  if (result.value("ok", false)) {
    json out = result;
    if (out.contains("response") && out["response"].is_object() && out["response"].contains("result") &&
        out["response"]["result"].is_object()) {
      out["messageId"] = out["response"]["result"].value("message_id", 0);
    }
    std::cout << out.dump(2) << std::endl;
    return 0;
  }

  std::cout << result.dump(2) << std::endl;
  return 1;
}

int runMessageDelete(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  std::string channel = argValue(pos, "--channel").value_or("");
  const std::string target = argValue(pos, "--target").value_or("");
  const std::string messageId = argValue(pos, "--message-id").value_or("");
  const bool dryRun = hasFlag(pos, "--dry-run");

  if (target.empty() || messageId.empty()) {
    std::cout << json{{"ok", false}, {"error", "--target and --message-id are required"}}.dump(2) << std::endl;
    return 1;
  }

  if (channel.empty()) {
    const auto channels = configuredChannels(cfg);
    if (channels.size() == 1) channel = channels[0];
  }
  if (channel != "telegram") {
    std::cout << json{{"ok", false}, {"error", "Only --channel telegram is implemented"}, {"channel", channel}}.dump(2)
              << std::endl;
    return 2;
  }

  json payload;
  std::string err;
  if (!parseTelegramTarget(target, payload, err)) {
    std::cout << json{{"ok", false}, {"channel", channel}, {"target", target}, {"error", err}}.dump(2) << std::endl;
    return 1;
  }

  try {
    payload["message_id"] = std::stoll(messageId);
  } catch (...) {
    std::cout << json{{"ok", false}, {"error", "--message-id must be numeric for telegram"}}.dump(2) << std::endl;
    return 1;
  }

  const auto result = callTelegramApi(cfg, "deleteMessage", payload, "message.delete", target, dryRun);
  std::cout << result.dump(2) << std::endl;
  return result.value("ok", false) ? 0 : 1;
}

int runMessageReact(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  std::string channel = argValue(pos, "--channel").value_or("");
  const std::string target = argValue(pos, "--target").value_or("");
  const std::string messageId = argValue(pos, "--message-id").value_or("");
  const std::string emoji = argValue(pos, "--emoji").value_or("👍");
  const bool remove = hasFlag(pos, "--remove");
  const bool dryRun = hasFlag(pos, "--dry-run");

  if (target.empty() || messageId.empty()) {
    std::cout << json{{"ok", false}, {"error", "--target and --message-id are required"}}.dump(2) << std::endl;
    return 1;
  }

  if (channel.empty()) {
    const auto channels = configuredChannels(cfg);
    if (channels.size() == 1) channel = channels[0];
  }
  if (channel != "telegram") {
    std::cout << json{{"ok", false}, {"error", "Only --channel telegram is implemented"}, {"channel", channel}}.dump(2)
              << std::endl;
    return 2;
  }

  json payload;
  std::string err;
  if (!parseTelegramTarget(target, payload, err)) {
    std::cout << json{{"ok", false}, {"channel", channel}, {"target", target}, {"error", err}}.dump(2) << std::endl;
    return 1;
  }

  try {
    payload["message_id"] = std::stoll(messageId);
  } catch (...) {
    std::cout << json{{"ok", false}, {"error", "--message-id must be numeric for telegram"}}.dump(2) << std::endl;
    return 1;
  }

  if (remove) {
    payload["reaction"] = json::array();
  } else {
    payload["reaction"] = json::array({{{"type", "emoji"}, {"emoji", emoji}}});
  }
  payload["is_big"] = false;

  const auto result = callTelegramApi(cfg, "setMessageReaction", payload, "message.react", target, dryRun);
  std::cout << result.dump(2) << std::endl;
  return result.value("ok", false) ? 0 : 1;
}

int runMessagePoll(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  std::string channel = argValue(pos, "--channel").value_or("");
  const std::string target = argValue(pos, "--target").value_or("");
  const std::string question = argValue(pos, "--poll-question").value_or("");
  const std::vector<std::string> options = argValues(pos, "--poll-option");
  const bool pollMulti = hasFlag(pos, "--poll-multi");
  const auto pollHours = argValue(pos, "--poll-duration-hours");
  const bool dryRun = hasFlag(pos, "--dry-run");

  if (target.empty() || question.empty() || options.size() < 2) {
    std::cout << json{{"ok", false},
                      {"error", "--target, --poll-question, and at least two --poll-option are required"}}
                     .dump(2)
              << std::endl;
    return 1;
  }

  if (channel.empty()) {
    const auto channels = configuredChannels(cfg);
    if (channels.size() == 1) channel = channels[0];
  }
  if (channel != "telegram") {
    std::cout << json{{"ok", false}, {"error", "Only --channel telegram is implemented"}, {"channel", channel}}.dump(2)
              << std::endl;
    return 2;
  }

  json payload;
  std::string err;
  if (!parseTelegramTarget(target, payload, err)) {
    std::cout << json{{"ok", false}, {"channel", channel}, {"target", target}, {"error", err}}.dump(2) << std::endl;
    return 1;
  }

  payload["question"] = question;
  payload["options"] = options;
  payload["allows_multiple_answers"] = pollMulti;
  payload["is_anonymous"] = false;

  if (pollHours.has_value()) {
    try {
      const int hours = std::max(1, std::stoi(*pollHours));
      payload["close_date"] = static_cast<int>(std::time(nullptr)) + hours * 3600;
    } catch (...) {
      std::cout << json{{"ok", false}, {"error", "--poll-duration-hours must be numeric"}}.dump(2) << std::endl;
      return 1;
    }
  }

  const auto result = callTelegramApi(cfg, "sendPoll", payload, "message.poll", target, dryRun);
  std::cout << result.dump(2) << std::endl;
  return result.value("ok", false) ? 0 : 1;
}

int runChannelsAction(const std::string& configPath, const std::vector<std::string>& pos) {
  if (pos.size() < 2) {
    std::cout << json{{"ok", false}, {"error", "Usage: channels list|status|capabilities|add|remove|resolve"}}.dump(2) << std::endl;
    return 1;
  }

  const std::string action = pos[1];
  auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);

  if (action == "list" || action == "status") {
    const char* token = std::getenv(cfg.telegram.botTokenEnv.c_str());
    const bool tokenPresent = token && std::string(token).size() > 0;
    const json telegram = {{"channel", "telegram"},
                           {"enabled", cfg.telegram.enabled},
                           {"tokenEnv", cfg.telegram.botTokenEnv},
                           {"tokenPresent", tokenPresent},
                           {"dmPolicy", cfg.telegram.dmPolicy}};
    std::cout << json{{"ok", true}, {"channels", json::array({telegram})}}.dump(2) << std::endl;
    return 0;
  }

  if (action == "capabilities") {
    const json caps = {{"send", true},
                       {"read", true},
                       {"react", true},
                       {"delete", true},
                       {"poll", true},
                       {"threads", true}};
    std::cout << json{{"ok", true}, {"channel", "telegram"}, {"capabilities", caps}}.dump(2) << std::endl;
    return 0;
  }

  if (action == "resolve") {
    const std::string channel = argValue(pos, "--channel").value_or("telegram");
    if (channel != "telegram") {
      std::cout << json{{"ok", false}, {"error", "resolve baseline supports --channel telegram only"}}.dump(2) << std::endl;
      return 2;
    }
    if (pos.size() < 3) {
      std::cout << json{{"ok", false}, {"error", "Usage: channels resolve --channel telegram <target>"}}.dump(2) << std::endl;
      return 1;
    }

    std::string target;
    for (size_t i = 2; i < pos.size(); ++i) {
      if (pos[i].rfind("--", 0) == 0) {
        ++i;
        continue;
      }
      target = pos[i];
      break;
    }

    if (target.empty()) {
      std::cout << json{{"ok", false}, {"error", "target is required"}}.dump(2) << std::endl;
      return 1;
    }

    json parsed;
    std::string err;
    if (!parseTelegramTarget(target, parsed, err)) {
      std::cout << json{{"ok", false}, {"target", target}, {"error", err}}.dump(2) << std::endl;
      return 1;
    }
    std::cout << json{{"ok", true}, {"channel", "telegram"}, {"input", target}, {"resolved", parsed}}.dump(2) << std::endl;
    return 0;
  }

  if (action == "add" || action == "remove") {
    const std::string channel = argValue(pos, "--channel").value_or("");
    if (channel != "telegram") {
      std::cout << json{{"ok", false}, {"error", "Only --channel telegram is implemented in current baseline"}}.dump(2) << std::endl;
      return 2;
    }

    auto j = loadJsonFile(configPath);
    if (action == "add") {
      j["telegram"]["enabled"] = true;
      if (const auto tokenEnv = argValue(pos, "--token-env"); tokenEnv.has_value()) j["telegram"]["botTokenEnv"] = *tokenEnv;
      if (const auto dmPolicy = argValue(pos, "--dm-policy"); dmPolicy.has_value()) j["telegram"]["dmPolicy"] = *dmPolicy;
      saveJsonFile(configPath, j);
      std::cout << json{{"ok", true}, {"action", "channels.add"}, {"channel", channel}, {"enabled", true}, {"config", configPath}}.dump(2) << std::endl;
      return 0;
    }

    j["telegram"]["enabled"] = false;
    saveJsonFile(configPath, j);
    std::cout << json{{"ok", true}, {"action", "channels.remove"}, {"channel", channel}, {"enabled", false}, {"config", configPath}}.dump(2) << std::endl;
    return 0;
  }

  std::cout << json{{"ok", false}, {"error", "Unsupported channels action in baseline"}, {"action", action}}.dump(2) << std::endl;
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
                      {"expiresAt", auth.expiresAt}, {"expired", auth.expired}, {"refreshed", auth.refreshed}, {"warnings", auth.warnings}});
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

int runGatewayProbe(const std::string& configPath, const std::vector<std::string>& pos) {
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const bool includeLocal = !hasFlag(pos, "--no-local");
  const std::string explicitUrl = argValue(pos, "--url").value_or("");

  json probes = json::array();
  if (includeLocal) {
    probes.push_back(json{{"target", "local"}, {"url", "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port)}});
  }
  if (!explicitUrl.empty()) {
    probes.push_back(json{{"target", "explicit"}, {"url", explicitUrl}});
  }

  json results = json::array();
  for (const auto& p : probes) {
    const std::string url = p.value("url", "");
    const auto health = httpGetJson(url + "/health", authHeaderFromEnv(cfg));
    const auto status = httpGetJson(url + "/api/status", authHeaderFromEnv(cfg));
    results.push_back(json{{"target", p.value("target", "unknown")},
                           {"url", url},
                           {"reachable", health.has_value() && health->value("ok", false)},
                           {"health", health.has_value() ? *health : json::object()},
                           {"status", status.has_value() ? *status : json::object()}});
  }

  std::cout << json{{"ok", true}, {"probes", results}}.dump(2) << std::endl;
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

  if (method == "nodes.list" || method == "nodes.status" || method == "nodes.describe" || method == "nodes.invoke") {
    const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
    const auto out = nodesMethod(cfg, method, params);
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : (out.value("error", "") == "invoke_not_available_in_baseline" ? 2 : 1);
  }

  if (method == "devices.list" || method == "devices.status" || method == "devices.invoke") {
    const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
    const auto out = devicesMethod(cfg, method, params);
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 2;
  }

  if (method == "canvas.status" || method == "canvas.invoke" || method == "canvas.snapshot") {
    const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
    const auto out = canvasMethod(cfg, method, params);
    std::cout << out.dump(2) << std::endl;
    return out.value("ok", false) ? 0 : 2;
  }

  if (method == "logs.tail") {
    const int lines = std::max(1, params.value("lines", params.value("limit", 50)));
    const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
    const std::filesystem::path p = cfg.audit.file;
    if (!std::filesystem::exists(p)) {
      std::cout << json{{"ok", false}, {"method", method}, {"error", "audit log file not found"}, {"path", p.string()}}.dump(2) << std::endl;
      return 1;
    }
    std::ifstream in(p);
    std::vector<std::string> all;
    std::string line;
    while (std::getline(in, line)) all.push_back(line);
    const size_t start = all.size() > static_cast<size_t>(lines) ? all.size() - static_cast<size_t>(lines) : 0;
    json tail = json::array();
    for (size_t i = start; i < all.size(); ++i) tail.push_back(all[i]);
    std::cout << json{{"ok", true}, {"method", method}, {"path", p.string()}, {"lines", tail}}.dump(2) << std::endl;
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
    if (hasEnvToken(cfg.gateway.auth.tokenEnv)) {
      report("gateway.auth.token", "OK", cfg.gateway.auth.tokenEnv);
      const char* token = std::getenv(cfg.gateway.auth.tokenEnv.c_str());
      if (token && std::string(token).size() < 16) {
        ++warnings;
        report("gateway.auth.token_strength", "WARN", ru ? "токен короче 16 символов" : "token is shorter than 16 chars");
      }
    } else { ++fails; report("gateway.auth.token", "FAIL", "missing env: " + cfg.gateway.auth.tokenEnv); }
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

  if (permsTooOpen(cfg.audit.file)) {
    ++warnings;
    if (fix) { tightenPermsOwnerOnly(cfg.audit.file); ++fixed; report(ru ? "Права audit log" : "Audit log permissions", "FIX", cfg.audit.file); }
    else report(ru ? "Права audit log" : "Audit log permissions", "WARN", ru ? "слишком широкие" : "too open");
  } else {
    report(ru ? "Права audit log" : "Audit log permissions", "OK", cfg.audit.file);
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
      if (pos.size() == 1 || (pos.size() >= 2 && (pos[1] == "run"))) command = "run";
      else if (pos.size() >= 2 && pos[1] == "status") return runGatewayStatus(configPath);
      else if (pos.size() >= 2 && pos[1] == "start") return runGatewayStart(configPath, programPath);
      else if (pos.size() >= 2 && pos[1] == "stop") return runGatewayStop(configPath);
      else if (pos.size() >= 2 && pos[1] == "restart") return runGatewayRestart(configPath, programPath);
      else if (pos.size() >= 2 && pos[1] == "health") return runHealth(configPath);
      else if (pos.size() >= 2 && pos[1] == "probe") return runGatewayProbe(configPath, pos);
      else if (pos.size() >= 2 && (pos[1] == "discover" || pos[1] == "install" || pos[1] == "uninstall")) {
        return printNotImplJson("gateway", pos[1], {"run", "status", "start", "stop", "restart", "health", "probe", "call"});
      } else if (pos.size() >= 2 && pos[1] == "call") return runGatewayCall(configPath, pos);
      else return printNotImplJson("gateway", (pos.size() >= 2 ? pos[1] : ""), {"run", "status", "start", "stop", "restart", "health", "probe", "call"});
    }

    if (command == "security") {
      if (pos.size() == 1 || (pos.size() >= 2 && pos[1] == "audit")) return runSecurityAudit(configPath, pos, lang);
      return printNotImplJson("security", (pos.size() >= 2 ? pos[1] : ""), {"audit"});
    }

    if (command == "browser" && pos.size() >= 2 && pos[1] == "status") return runBrowserStatus(configPath);
    if (command == "browser" && pos.size() >= 3 && pos[1] == "open") return runBrowserOpen(configPath, pos[2]);
    if (command == "browser" && pos.size() >= 3 && pos[1] == "navigate") return runBrowserNavigate(configPath, pos);
    if (command == "browser" && pos.size() >= 2 && pos[1] == "snapshot") return runBrowserSnapshot(configPath, pos);
    if (command == "browser" && pos.size() >= 3 && pos[1] == "click") return runBrowserClick(configPath, pos);
    if (command == "browser" && pos.size() >= 4 && pos[1] == "type") return runBrowserType(configPath, pos);
    if (command == "browser" && pos.size() >= 2 && pos[1] == "screenshot") return runBrowserScreenshot(configPath, pos);
    if (command == "cron" && pos.size() >= 2 && pos[1] == "status") return runCronAction(configPath, "status");
    if (command == "cron" && pos.size() >= 2 && pos[1] == "list") return runCronAction(configPath, "list");
    if (command == "cron" && pos.size() >= 3 && (pos[1] == "get" || pos[1] == "show")) return runCronAction(configPath, "get", pos[2]);
    if (command == "cron" && pos.size() >= 2 && pos[1] == "add") {
      auto j = parseJsonArg(pos);
      if (!j.has_value()) { std::cerr << "Missing --json payload" << std::endl; return 1; }
      return runCronAction(configPath, "add", *j);
    }
    if (command == "cron" && pos.size() >= 2 && pos[1] == "validate") {
      auto j = parseJsonArg(pos);
      if (!j.has_value()) { std::cerr << "Missing --json payload" << std::endl; return 1; }
      return runCronAction(configPath, "validate", *j);
    }
    if (command == "cron" && pos.size() >= 3 && pos[1] == "edit") {
      auto j = parseJsonArg(pos);
      if (!j.has_value()) { std::cerr << "Missing --json patch" << std::endl; return 1; }
      return runCronAction(configPath, "edit", pos[2], *j);
    }
    if (command == "cron" && pos.size() >= 3 && (pos[1] == "rm" || pos[1] == "remove")) return runCronAction(configPath, "rm", pos[2]);
    if (command == "cron" && pos.size() >= 3 && pos[1] == "enable") return runCronAction(configPath, "enable", pos[2]);
    if (command == "cron" && pos.size() >= 3 && pos[1] == "disable") return runCronAction(configPath, "disable", pos[2]);
    if (command == "cron" && pos.size() >= 3 && pos[1] == "runs") {
      const auto limit = argValue(pos, "--limit").value_or("20");
      return runCronAction(configPath, "runs", pos[2], limit);
    }
    if (command == "cron" && pos.size() >= 3 && pos[1] == "run") {
      const std::string mode = hasFlag(pos, "--due") ? "due" : "force";
      return runCronAction(configPath, "run", pos[2], mode);
    }
    if (command == "cron") {
      return printNotImplJson("cron", (pos.size() >= 2 ? pos[1] : ""), {"status", "list", "get", "add", "edit", "enable", "disable", "run", "runs", "validate", "rm"});
    }

    if (command == "nodes" || command == "node") {
      return runNodesFamily(configPath, pos, command);
    }

    if (command == "devices") {
      return runDevicesFamily(configPath, pos);
    }

    if (command == "canvas") {
      return runCanvasFamily(configPath, pos);
    }

    if (command == "message") {
      if (pos.size() >= 2 && pos[1] == "send") return runMessageSend(configPath, pos);
      if (pos.size() >= 2 && pos[1] == "react") return runMessageReact(configPath, pos);
      if (pos.size() >= 2 && pos[1] == "delete") return runMessageDelete(configPath, pos);
      if (pos.size() >= 2 && pos[1] == "poll") return runMessagePoll(configPath, pos);
      return (printCompatNotImplemented("message " + (pos.size() >= 2 ? pos[1] : ""), lang), 2);
    }

    if (command == "channels") {
      return runChannelsAction(configPath, pos);
    }

    if (command == "agent" || command == "agents") {
      return runAgentsFamily(configPath, pos, command);
    }

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

    std::set<std::string> compatTop = {"dashboard","reset","uninstall","update","acp","memory","approvals","sandbox","dns","docs","hooks","webhooks","plugins","skills","tui","voicecall","directory"};
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
