#include "browser/BrowserRelay.hpp"

#include "util/Shell.hpp"

namespace clawforge::browser {

BrowserRelay::BrowserRelay(core::BrowserConfig config) : config_(std::move(config)) {}

nlohmann::json BrowserRelay::status() const {
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
        {"note", "Stage 6 baseline: open is real; snapshot is diagnostic stub."}}},
  };
}

nlohmann::json BrowserRelay::open(const std::string& url) const {
  if (!config_.enabled) {
    return {{"ok", false}, {"error", "browser relay disabled in config.browser.enabled"}};
  }

  if (url.empty()) {
    return {{"ok", false}, {"error", "url is required"}};
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

nlohmann::json BrowserRelay::snapshot(const std::string& urlHint) const {
  return {{"ok", false},
          {"implemented", false},
          {"backend", config_.backend},
          {"urlHint", urlHint},
          {"error", "Snapshot is not implemented in Stage 6 baseline"},
          {"diagnostics",
           {{"hint", "Use /api/browser/open first and inspect page manually"},
            {"nextStep", "Integrate Playwright capture in Stage 7"}}}};
}

}  // namespace clawforge::browser
