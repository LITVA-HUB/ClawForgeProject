#include <csignal>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/Application.hpp"
#include "automation/CronScheduler.hpp"
#include "browser/BrowserRelay.hpp"
#include "channels/TelegramPairingStore.hpp"
#include "core/Config.hpp"
#include "core/EventBus.hpp"
#include "core/Logger.hpp"
#include "session/SessionStore.hpp"
#include "tools/BuiltinTools.hpp"
#include "tools/ToolRegistry.hpp"
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

void printCompatNotImplemented(const std::string& cmd, const std::string& lang) {
  std::cout << (lang == "ru" ? "Команда совместимости OpenClaw пока не реализована: " : "OpenClaw compatibility command is not implemented yet: ")
            << cmd << "\n" << (lang == "ru" ? "Смотри docs/CLI_PARITY.md" : "See docs/CLI_PARITY.md") << std::endl;
}

void printHelp(const std::string& lang) {
  const bool ru = (lang == "ru");
  std::cout << (ru ? "ClawForge CLI\n\n" : "ClawForge CLI\n\n");
  std::cout << "Usage:\n  clawforge [run] [--config <path>]\n  clawforge status|health|doctor|sessions\n  clawforge cron list\n  clawforge tools list\n  clawforge pairing list|approve <code>\n  clawforge config get <key>|set <key> <value>\n  clawforge models list|status|set <provider/model|alias>\n  clawforge models aliases list|add|remove\n  clawforge models fallbacks list|add|remove|clear\n";
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

int runStatus(const std::string& configPath, const std::string& lang) {
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
  for (const auto& [name, p] : cfg.modelProviders) providers.push_back({{"provider", name}, {"apiStyle", p.apiStyle}, {"endpoint", p.endpoint}, {"apiKeyEnv", p.apiKeyEnv}, {"keyPresent", (std::getenv(p.apiKeyEnv.c_str()) && std::string(std::getenv(p.apiKeyEnv.c_str())).size() > 0)}});
  std::cout << json({{"ok", true}, {"current", cfg.modelRouting.current}, {"providers", providers}}).dump(2) << std::endl; return 0;
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
    const char* key = std::getenv(p.apiKeyEnv.c_str());
    bool present = key && std::string(key).size() > 0;
    checks.push_back({{"provider", name}, {"endpoint", p.endpoint}, {"apiStyle", p.apiStyle}, {"apiKeyEnv", p.apiKeyEnv}, {"keyPresent", present}});
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

    std::set<std::string> compatTop = {"setup","onboard","configure","dashboard","reset","uninstall","update","message","agent","agents","acp","gateway","memory","nodes","devices","node","approvals","sandbox","dns","docs","hooks","webhooks","plugins","channels","security","skills","tui"};
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
