#include "browser/BrowserRelay.hpp"

#include <vector>

#include "util/Shell.hpp"

namespace clawforge::browser {

BrowserRelay::BrowserRelay(core::BrowserConfig config) : config_(std::move(config)) {}

bool BrowserRelay::useOpenClawCli() const {
  return config_.backend == "openclaw_cli" || config_.backend == "openclaw-cli" ||
         config_.backend == "openclaw";
}

nlohmann::json BrowserRelay::runOpenClawBrowser(const std::vector<std::string>& args) const {
  if (config_.cliBinary.empty()) {
    return {{"ok", false}, {"error", "browser.cliBinary is empty for openclaw_cli backend"}};
  }

  std::string cmd = util::Shell::quote(config_.cliBinary) + " browser --json";
  if (!config_.profile.empty()) {
    cmd += " --browser-profile " + util::Shell::quote(config_.profile);
  }
  for (const auto& arg : args) {
    cmd += " " + util::Shell::quote(arg);
  }

  const auto res = util::Shell::run(cmd);
  if (res.exitCode != 0) {
    return {{"ok", false},
            {"backend", "openclaw_cli"},
            {"profile", config_.profile},
            {"error", "openclaw browser command failed"},
            {"cmd", cmd},
            {"output", res.output}};
  }

  auto parsed = nlohmann::json::parse(res.output, nullptr, false);
  if (parsed.is_discarded()) {
    return {{"ok", false},
            {"backend", "openclaw_cli"},
            {"profile", config_.profile},
            {"error", "openclaw browser returned non-json output"},
            {"cmd", cmd},
            {"raw", res.output}};
  }

  if (!parsed.is_object()) {
    return {{"ok", true},
            {"backend", "openclaw_cli"},
            {"profile", config_.profile},
            {"data", parsed}};
  }

  parsed["backend"] = "openclaw_cli";
  parsed["profile"] = config_.profile;
  if (!parsed.contains("ok")) parsed["ok"] = true;
  return parsed;
}

nlohmann::json BrowserRelay::status() const {
  if (useOpenClawCli()) {
    return runOpenClawBrowser({"status"});
  }

  const auto hasOpen = util::Shell::run("command -v open");
  const auto hasPlaywright = util::Shell::run("command -v playwright");

  return {
      {"ok", true},
      {"enabled", config_.enabled},
      {"backend", config_.backend},
      {"diagnostics",
       {{"openCommand", config_.openCommand},
        {"openAvailable", hasOpen.exitCode == 0},
        {"playwrightCliAvailable", hasPlaywright.exitCode == 0},
        {"note", "openclaw_cli backend provides real snapshots/actions via OpenClaw CLI."}}},
  };
}

nlohmann::json BrowserRelay::open(const std::string& url) const {
  if (!config_.enabled) {
    return {{"ok", false}, {"error", "browser relay disabled in config.browser.enabled"}};
  }

  if (url.empty()) {
    return {{"ok", false}, {"error", "url is required"}};
  }

  if (useOpenClawCli()) {
    return runOpenClawBrowser({"open", url});
  }

  if (config_.backend == "stub") {
    return {{"ok", true},
            {"mode", "stub"},
            {"opened", false},
            {"message", "Stub backend enabled. No browser process was started."},
            {"url", url}};
  }

  if (config_.backend == "shell") {
    const std::string cmd = config_.openCommand + " " + util::Shell::quote(url);
    const auto res = util::Shell::run(cmd);
    if (res.exitCode != 0) {
      return {{"ok", false}, {"error", "Failed to open URL via shell backend"}, {"cmd", cmd}, {"output", res.output}};
    }

    return {{"ok", true}, {"mode", "shell"}, {"opened", true}, {"url", url}, {"cmd", cmd}};
  }

  return {{"ok", false}, {"error", "Unknown browser backend: " + config_.backend}};
}

nlohmann::json BrowserRelay::snapshot(const std::string& urlHint, const std::string& targetId) const {
  if (!config_.enabled) {
    return {{"ok", false}, {"error", "browser relay disabled in config.browser.enabled"}};
  }

  if (useOpenClawCli()) {
    nlohmann::json nav = nlohmann::json::object();
    if (!urlHint.empty()) {
      std::vector<std::string> navArgs{"navigate", urlHint};
      if (!targetId.empty()) {
        navArgs.push_back("--target-id");
        navArgs.push_back(targetId);
      }
      nav = runOpenClawBrowser(navArgs);
      if (!nav.value("ok", false)) {
        return {{"ok", false},
                {"error", "navigate failed before snapshot"},
                {"urlHint", urlHint},
                {"targetId", targetId},
                {"navigate", nav}};
      }
    }

    std::vector<std::string> snapArgs{"snapshot", "--format", "ai", "--limit", "800"};
    if (!targetId.empty()) {
      snapArgs.push_back("--target-id");
      snapArgs.push_back(targetId);
    }
    auto snap = runOpenClawBrowser(snapArgs);
    if (!snap.is_object()) return snap;
    if (!urlHint.empty()) snap["navigate"] = nav;
    if (!targetId.empty() && !snap.contains("requestedTargetId")) snap["requestedTargetId"] = targetId;
    return snap;
  }

  return {{"ok", false},
          {"implemented", false},
          {"backend", config_.backend},
          {"urlHint", urlHint},
          {"targetId", targetId},
          {"error", "Snapshot is not implemented for this backend. Use browser.backend=openclaw_cli"},
          {"diagnostics",
           {{"hint", "Configure browser.backend=openclaw_cli and browser.profile=openclaw"},
            {"nextStep", "Then use /api/browser/snapshot, /api/browser/click, /api/browser/type"}}}};
}

nlohmann::json BrowserRelay::navigate(const std::string& url, const std::string& targetId) const {
  if (!config_.enabled) {
    return {{"ok", false}, {"error", "browser relay disabled in config.browser.enabled"}};
  }
  if (url.empty()) return {{"ok", false}, {"error", "url is required"}};

  if (useOpenClawCli()) {
    std::vector<std::string> args{"navigate", url};
    if (!targetId.empty()) {
      args.push_back("--target-id");
      args.push_back(targetId);
    }
    return runOpenClawBrowser(args);
  }

  return {{"ok", false},
          {"error", "navigate is not implemented for backend: " + config_.backend},
          {"hint", "Use browser.backend=openclaw_cli"}};
}

nlohmann::json BrowserRelay::click(const std::string& ref, const std::string& targetId,
                                   bool doubleClick) const {
  if (!config_.enabled) {
    return {{"ok", false}, {"error", "browser relay disabled in config.browser.enabled"}};
  }
  if (ref.empty()) return {{"ok", false}, {"error", "ref is required"}};

  if (useOpenClawCli()) {
    std::vector<std::string> args{"click", ref};
    if (!targetId.empty()) {
      args.push_back("--target-id");
      args.push_back(targetId);
    }
    if (doubleClick) args.push_back("--double");
    return runOpenClawBrowser(args);
  }

  return {{"ok", false},
          {"error", "click is not implemented for backend: " + config_.backend},
          {"hint", "Use browser.backend=openclaw_cli"}};
}

nlohmann::json BrowserRelay::type(const std::string& ref, const std::string& text,
                                  const std::string& targetId, bool submit,
                                  bool slowly) const {
  if (!config_.enabled) {
    return {{"ok", false}, {"error", "browser relay disabled in config.browser.enabled"}};
  }
  if (ref.empty()) return {{"ok", false}, {"error", "ref is required"}};

  if (useOpenClawCli()) {
    std::vector<std::string> args{"type", ref, text};
    if (!targetId.empty()) {
      args.push_back("--target-id");
      args.push_back(targetId);
    }
    if (submit) args.push_back("--submit");
    if (slowly) args.push_back("--slowly");
    return runOpenClawBrowser(args);
  }

  return {{"ok", false},
          {"error", "type is not implemented for backend: " + config_.backend},
          {"hint", "Use browser.backend=openclaw_cli"}};
}

nlohmann::json BrowserRelay::screenshot(const std::string& targetId, bool fullPage,
                                        const std::string& type) const {
  if (!config_.enabled) {
    return {{"ok", false}, {"error", "browser relay disabled in config.browser.enabled"}};
  }

  if (useOpenClawCli()) {
    std::vector<std::string> args{"screenshot"};
    if (!targetId.empty()) args.push_back(targetId);
    if (fullPage) args.push_back("--full-page");
    if (!type.empty()) {
      args.push_back("--type");
      args.push_back(type);
    }
    return runOpenClawBrowser(args);
  }

  return {{"ok", false},
          {"error", "screenshot is not implemented for backend: " + config_.backend},
          {"hint", "Use browser.backend=openclaw_cli"}};
}

}  // namespace clawforge::browser
