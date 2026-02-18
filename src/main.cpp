#include <csignal>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/Application.hpp"
#include "automation/CronScheduler.hpp"
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

std::string normalizeLang(std::string lang) {
  for (char& ch : lang) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (lang.rfind("en", 0) == 0) return "en";
  if (lang.rfind("ru", 0) == 0) return "ru";
  return "ru";
}

std::string detectLang() {
  const char* envLang = std::getenv("LANG");
  if (!envLang || std::string(envLang).empty()) {
    return "ru";
  }
  return normalizeLang(envLang);
}

void onSignal(int) {
  if (g_app) {
    clawforge::core::Logger::info("Signal received, stopping...");
    g_app->stop();
  }
}

std::string authHeaderFromEnv(const clawforge::core::AppConfig& cfg) {
  if (cfg.gateway.auth.mode != "token") return "";
  const char* token = std::getenv(cfg.gateway.auth.tokenEnv.c_str());
  if (!token || std::string(token).empty()) return "";
  return "Authorization: Bearer " + std::string(token);
}

std::optional<json> httpGetJson(const std::string& url, const std::string& authHeader = "") {
  std::string cmd = "curl -sS --max-time 2 " + clawforge::util::Shell::quote(url);
  if (!authHeader.empty()) {
    cmd += " -H " + clawforge::util::Shell::quote(authHeader);
  }
  const auto res = clawforge::util::Shell::run(cmd);
  if (res.exitCode != 0) return std::nullopt;
  auto parsed = json::parse(res.output, nullptr, false);
  if (parsed.is_discarded()) return std::nullopt;
  return parsed;
}

void printHelp(const std::string& lang) {
  const bool ru = (lang == "ru");
  if (ru) {
    std::cout << R"(ClawForge CLI

Использование:
  clawforge [--lang ru|en] [run] [--config <path>]
  clawforge [--lang ru|en] --doctor [--config <path>]
  clawforge [--lang ru|en] --init-config [--config <path>]
  clawforge [--lang ru|en] status [--config <path>]
  clawforge [--lang ru|en] cron list [--config <path>]
  clawforge [--lang ru|en] tools list [--config <path>]
  clawforge [--lang ru|en] pairing list [--config <path>]
  clawforge [--lang ru|en] pairing approve <code> [--config <path>]
  clawforge --help

Команды:
  run            Запустить сервис (по умолчанию, если команда не указана)
  status         Показать статус (через API, если сервер доступен; иначе локально)
  cron list      Показать cron jobs (через API или из state)
  tools list     Показать tools и policy (через API или локально)
  --doctor       Проверить конфиг, папки и переменные окружения
  --init-config  Создать config из примера (если файла ещё нет)
  pairing list   Показать запросы/одобрения Telegram pairing
  pairing approve <code>  Одобрить pairing по коду
  --help         Показать эту справку

Опции:
  --lang ru|en   Язык CLI (если не указан, определяется по LANG; по умолчанию ru)
  --config PATH  Путь к config.json (по умолчанию: config/config.json)
)";
  } else {
    std::cout << R"(ClawForge CLI

Usage:
  clawforge [--lang ru|en] [run] [--config <path>]
  clawforge [--lang ru|en] --doctor [--config <path>]
  clawforge [--lang ru|en] --init-config [--config <path>]
  clawforge [--lang ru|en] status [--config <path>]
  clawforge [--lang ru|en] cron list [--config <path>]
  clawforge [--lang ru|en] tools list [--config <path>]
  clawforge [--lang ru|en] pairing list [--config <path>]
  clawforge [--lang ru|en] pairing approve <code> [--config <path>]
  clawforge --help

Commands:
  run            Start service (default when no command is provided)
  status         Show status (tries API, falls back to local state)
  cron list      List cron jobs (via API or local state)
  tools list     List tools and policy (via API or local)
  --doctor       Check config, directories and required env vars
  --init-config  Create config from example (if missing)
  pairing list   List Telegram pairing requests/approvals
  pairing approve <code>  Approve Telegram pairing by code
  --help         Show this help

Options:
  --lang ru|en   CLI language (auto from LANG; default ru)
  --config PATH  Path to config.json (default: config/config.json)
)";
  }
}

int runDoctor(const std::string& configPath, const std::string& lang) {
  const bool ru = (lang == "ru");
  bool ok = true;

  auto report = [&](const std::string& title, bool pass, const std::string& details) {
    std::cout << (pass ? "[OK] " : "[FAIL] ") << title;
    if (!details.empty()) {
      std::cout << " — " << details;
    }
    std::cout << std::endl;
  };

  report(ru ? "CLI язык" : "CLI language", true, ru ? "русский" : "English");

  if (std::filesystem::exists(configPath)) {
    report(ru ? "Файл конфига" : "Config file", true, configPath);
  } else {
    report(ru ? "Файл конфига" : "Config file", false,
           ru ? "не найден: " + configPath : "not found: " + configPath);
    ok = false;
  }

  clawforge::core::AppConfig cfg;
  bool loaded = false;
  try {
    cfg = clawforge::core::AppConfig::loadFromFile(configPath);
    loaded = true;
    report(ru ? "Парсинг config.json" : "config.json parse", true, "");
  } catch (const std::exception& e) {
    report(ru ? "Парсинг config.json" : "config.json parse", false, e.what());
    ok = false;
  }

  if (loaded) {
    report(ru ? "Workspace" : "Workspace", std::filesystem::exists(cfg.workspace),
           cfg.workspace.string());
    if (!std::filesystem::exists(cfg.workspace)) ok = false;

    report(ru ? "State dir" : "State dir", std::filesystem::exists(cfg.stateDir),
           cfg.stateDir.string());
    if (!std::filesystem::exists(cfg.stateDir)) ok = false;

    const auto perms = std::filesystem::status(cfg.stateDir).permissions();
    const bool writable = (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
    report(ru ? "Права state dir" : "State dir permissions", writable,
           writable ? (ru ? "запись доступна" : "owner write is set")
                    : (ru ? "нет owner-write" : "owner write missing"));
    if (!writable) ok = false;

    const char* key = std::getenv(cfg.model.apiKeyEnv.c_str());
    const bool keyPresent = key && std::string(key).size() > 0;
    report(ru ? "Ключ LLM" : "LLM key", keyPresent,
           (ru ? "env: " : "env: ") + cfg.model.apiKeyEnv);
    if (!keyPresent) ok = false;

    if (cfg.telegram.enabled) {
      const char* tg = std::getenv(cfg.telegram.botTokenEnv.c_str());
      const bool tgPresent = tg && std::string(tg).size() > 0;
      report(ru ? "Токен Telegram" : "Telegram token", tgPresent,
             (ru ? "env: " : "env: ") + cfg.telegram.botTokenEnv);
      if (!tgPresent) ok = false;
    }

    const bool authModeValid = (cfg.gateway.auth.mode == "off" || cfg.gateway.auth.mode == "token");
    report(ru ? "Режим auth" : "Auth mode", authModeValid, cfg.gateway.auth.mode);
    if (!authModeValid) ok = false;

    if (cfg.gateway.auth.mode == "token") {
      const char* gw = std::getenv(cfg.gateway.auth.tokenEnv.c_str());
      const bool gwPresent = gw && std::string(gw).size() > 0;
      report(ru ? "Gateway Bearer токен" : "Gateway Bearer token", gwPresent,
             (ru ? "env: " : "env: ") + cfg.gateway.auth.tokenEnv);
      if (!gwPresent) ok = false;
    }

    const auto openCheck = clawforge::util::Shell::run("command -v open");
    const bool browserReady = !cfg.browser.enabled || cfg.browser.backend == "stub" || openCheck.exitCode == 0;
    report(ru ? "Browser backend readiness" : "Browser backend readiness", browserReady,
           "backend=" + cfg.browser.backend);
    if (!browserReady) ok = false;
  }

  if (ok) {
    std::cout << (ru ? "\nДиагностика завершена: всё хорошо ✅"
                     : "\nDoctor finished: everything looks good ✅")
              << std::endl;
    return 0;
  }

  std::cout << (ru ? "\nДиагностика завершена: есть проблемы. Исправь FAIL-пункты выше."
                   : "\nDoctor finished: issues found. Fix FAIL items above.")
            << std::endl;
  return 1;
}

int initConfig(const std::string& configPath, const std::string& lang) {
  const bool ru = (lang == "ru");
  const std::filesystem::path dst(configPath);
  const std::filesystem::path src("config/config.example.json");

  if (std::filesystem::exists(dst)) {
    std::cout << (ru ? "Конфиг уже существует: " : "Config already exists: ") << dst.string()
              << std::endl;
    return 0;
  }

  if (!std::filesystem::exists(src)) {
    std::cerr << (ru ? "Не найден шаблон: " : "Template not found: ") << src.string()
              << std::endl;
    return 1;
  }

  std::filesystem::create_directories(dst.parent_path());
  std::filesystem::copy_file(src, dst);
  std::cout << (ru ? "Создан конфиг: " : "Created config: ") << dst.string() << std::endl;
  return 0;
}

int runStatus(const std::string& configPath, const std::string& lang) {
  const bool ru = (lang == "ru");
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);

  const auto remote = httpGetJson(baseUrl + "/api/status", authHeaderFromEnv(cfg));
  if (remote.has_value() && remote->value("ok", false)) {
    std::cout << (ru ? "Статус (через HTTP API):\n" : "Status (from HTTP API):\n")
              << remote->dump(2) << std::endl;
    return 0;
  }

  clawforge::session::SessionStore sessions(cfg.stateDir);
  sessions.init();
  clawforge::core::EventBus bus;
  clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
  cron.init();
  clawforge::tools::ToolRegistry tools;
  clawforge::tools::registerBuiltinTools(tools, cfg.workspace);
  tools.setPolicy(cfg.toolsPolicy);

  json local = {{"ok", true},
                {"mode", "local"},
                {"service", cfg.name},
                {"http", cfg.http.host + ":" + std::to_string(cfg.http.port)},
                {"sessions", {{"count", sessions.listSessions().size()}}},
                {"jobs", {{"count", cron.listJobs().size()}}},
                {"tools", {{"count", tools.list().size()}, {"allowed", tools.allowedTools()}}}};

  std::cout << (ru ? "Статус (локальный fallback):\n" : "Status (local fallback):\n")
            << local.dump(2) << std::endl;
  return 0;
}

int runCronList(const std::string& configPath, const std::string& lang) {
  const bool ru = (lang == "ru");
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);

  const auto remote = httpGetJson(baseUrl + "/api/cron/jobs", authHeaderFromEnv(cfg));
  if (remote.has_value() && remote->value("ok", false)) {
    std::cout << (ru ? "Cron jobs (через HTTP API):\n" : "Cron jobs (from HTTP API):\n")
              << remote->dump(2) << std::endl;
    return 0;
  }

  clawforge::core::EventBus bus;
  clawforge::automation::CronScheduler cron(cfg.stateDir, cfg.cron.tickMs, [](const auto&) {}, bus);
  if (!cron.init()) {
    std::cerr << (ru ? "Не удалось прочитать cron state" : "Failed to read cron state") << std::endl;
    return 1;
  }

  json arr = json::array();
  for (const auto& job : cron.listJobs()) {
    arr.push_back({{"id", job.id}, {"name", job.name}, {"kind", job.kind}, {"nextRunAt", job.nextRunAt},
                   {"enabled", job.enabled}, {"message", job.message}});
  }

  std::cout << (ru ? "Cron jobs (локальный fallback):\n" : "Cron jobs (local fallback):\n")
            << json({{"ok", true}, {"mode", "local"}, {"jobs", arr}}).dump(2) << std::endl;
  return 0;
}

int runToolsList(const std::string& configPath, const std::string& lang) {
  const bool ru = (lang == "ru");
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  const std::string baseUrl = "http://" + cfg.http.host + ":" + std::to_string(cfg.http.port);

  const auto remote = httpGetJson(baseUrl + "/api/tools", authHeaderFromEnv(cfg));
  if (remote.has_value() && remote->value("ok", false)) {
    std::cout << (ru ? "Tools (через HTTP API):\n" : "Tools (from HTTP API):\n")
              << remote->dump(2) << std::endl;
    return 0;
  }

  clawforge::tools::ToolRegistry tools;
  clawforge::tools::registerBuiltinTools(tools, cfg.workspace);
  tools.setPolicy(cfg.toolsPolicy);
  json local = {{"ok", true}, {"mode", "local"}, {"tools", tools.list()}, {"allowedTools", tools.allowedTools()}};

  std::cout << (ru ? "Tools (локальный fallback):\n" : "Tools (local fallback):\n")
            << local.dump(2) << std::endl;
  return 0;
}

int runPairingList(const std::string& configPath, const std::string& lang) {
  const bool ru = (lang == "ru");
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  clawforge::channels::TelegramPairingStore store(cfg.stateDir);
  if (!store.init()) {
    std::cerr << (ru ? "Не удалось инициализировать pairing store"
                     : "Failed to initialize pairing store")
              << std::endl;
    return 1;
  }

  std::cout << (ru ? "Pairing requests:" : "Pairing requests:") << std::endl;
  for (const auto& req : store.listRequests()) {
    std::cout << "- code=" << req.code << " userId=" << req.userId << " chatId=" << req.chatId
              << " approved=" << (req.approved ? "yes" : "no") << std::endl;
  }

  std::cout << "\n" << (ru ? "Approved:" : "Approved:") << std::endl;
  for (const auto& req : store.listApproved()) {
    std::cout << "- userId=" << req.userId << " code=" << req.code << " approvedAt=" << req.approvedAt
              << std::endl;
  }
  return 0;
}

int runPairingApprove(const std::string& configPath, const std::string& code, const std::string& lang) {
  const bool ru = (lang == "ru");
  const auto cfg = clawforge::core::AppConfig::loadFromFile(configPath);
  clawforge::channels::TelegramPairingStore store(cfg.stateDir);
  if (!store.init()) {
    std::cerr << (ru ? "Не удалось инициализировать pairing store"
                     : "Failed to initialize pairing store")
              << std::endl;
    return 1;
  }

  const auto approved = store.approveByCode(code);
  if (!approved.has_value()) {
    std::cerr << (ru ? "Код не найден: " : "Code not found: ") << code << std::endl;
    return 1;
  }

  std::cout << (ru ? "Одобрено: userId=" : "Approved: userId=") << approved->userId << " code="
            << approved->code << std::endl;
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::string command = "run";
    std::string subAction;
    std::string subValue;
    std::string configPath = "config/config.json";
    std::string lang = detectLang();

    std::vector<std::string> args;
    args.reserve(argc > 0 ? static_cast<size_t>(argc - 1) : 0);
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }

    for (size_t i = 0; i < args.size(); ++i) {
      const auto& arg = args[i];
      if (arg == "--lang") {
        if (i + 1 >= args.size()) {
          std::cerr << "--lang requires value ru|en" << std::endl;
          return 1;
        }
        lang = normalizeLang(args[++i]);
      } else if (arg == "--config") {
        if (i + 1 >= args.size()) {
          std::cerr << "--config requires path" << std::endl;
          return 1;
        }
        configPath = args[++i];
      } else if (arg == "--help" || arg == "-h") {
        command = "help";
      } else if (arg == "--doctor") {
        command = "doctor";
      } else if (arg == "--init-config") {
        command = "init-config";
      } else if (arg == "run") {
        command = "run";
      } else if (arg == "status") {
        command = "status";
      } else if (arg == "cron" || arg == "tools" || arg == "pairing") {
        command = arg;
        if (i + 1 >= args.size()) {
          std::cerr << (lang == "ru" ? "Ожидается подкоманда" : "Subcommand is required") << std::endl;
          return 1;
        }
        subAction = args[++i];
        if (command == "pairing" && subAction == "approve") {
          if (i + 1 >= args.size()) {
            std::cerr << (lang == "ru" ? "Ожидается код approve" : "Approve code is required")
                      << std::endl;
            return 1;
          }
          subValue = args[++i];
        }
      } else {
        std::cerr << (lang == "ru" ? "Неизвестный аргумент: " : "Unknown argument: ") << arg
                  << std::endl;
        printHelp(lang);
        return 1;
      }
    }

    if (command == "help") return (printHelp(lang), 0);
    if (command == "doctor") return runDoctor(configPath, lang);
    if (command == "init-config") return initConfig(configPath, lang);
    if (command == "status") return runStatus(configPath, lang);
    if (command == "cron" && subAction == "list") return runCronList(configPath, lang);
    if (command == "tools" && subAction == "list") return runToolsList(configPath, lang);
    if (command == "pairing" && subAction == "list") return runPairingList(configPath, lang);
    if (command == "pairing" && subAction == "approve") return runPairingApprove(configPath, subValue, lang);
    if ((command == "cron" || command == "tools") && subAction != "list") {
      std::cerr << (lang == "ru" ? "Поддерживается только subcommand list" : "Only subcommand 'list' is supported") << std::endl;
      return 1;
    }

    auto config = clawforge::core::AppConfig::loadFromFile(configPath);

    clawforge::app::Application app(std::move(config), lang);
    g_app = &app;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    return app.run();
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }
}
