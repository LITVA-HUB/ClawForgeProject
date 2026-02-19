#include "browser/BrowserRelay.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "util/Shell.hpp"

namespace clawforge::browser {

namespace {

std::string normalizeBackend(std::string backend) {
  for (char& c : backend) {
    if (c == '-') c = '_';
  }
  return backend;
}

std::filesystem::path nativeStatePath() {
  return std::filesystem::temp_directory_path() / "nexaclaw-native-browser-state.json";
}

std::string decodeDataHtml(const std::string& url) {
  const std::string prefix = "data:text/html,";
  if (url.rfind(prefix, 0) != 0) return "";

  std::string payload = url.substr(prefix.size());
  std::string out;
  out.reserve(payload.size());
  for (size_t i = 0; i < payload.size(); ++i) {
    if (payload[i] == '%' && i + 2 < payload.size()) {
      const std::string hex = payload.substr(i + 1, 2);
      char* end = nullptr;
      const long code = std::strtol(hex.c_str(), &end, 16);
      if (end && *end == '\0') {
        out.push_back(static_cast<char>(code));
        i += 2;
        continue;
      }
    }
    if (payload[i] == '+') {
      out.push_back(' ');
    } else {
      out.push_back(payload[i]);
    }
  }
  return out;
}

}  // namespace

BrowserRelay::BrowserRelay(core::BrowserConfig config) : config_(std::move(config)) {}

bool BrowserRelay::useOpenClawCli() const {
  const auto backend = normalizeBackend(config_.backend);
  return backend == "openclaw_cli" || backend == "openclaw";
}

bool BrowserRelay::useNativeBackend() const {
  return normalizeBackend(config_.backend) == "native";
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

void BrowserRelay::nativeLoadStateLocked() const {
  nativeTargets_.clear();
  nativeCounter_ = 0;

  std::ifstream in(nativeStatePath());
  if (!in) return;
  auto j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return;

  nativeCounter_ = j.value("counter", 0);
  const auto& targets = j["targets"];
  if (!targets.is_array()) return;
  for (const auto& item : targets) {
    if (!item.is_object()) continue;
    NativeTarget t;
    t.targetId = item.value("targetId", "");
    t.url = item.value("url", "");
    t.title = item.value("title", "");
    t.html = item.value("html", "");

    if (item.contains("refs") && item["refs"].is_object()) {
      for (auto it = item["refs"].begin(); it != item["refs"].end(); ++it) {
        NativeRef r;
        r.role = it.value().value("role", "");
        r.name = it.value().value("name", "");
        r.text = it.value().value("text", "");
        t.refs[it.key()] = r;
      }
    }
    if (item.contains("typedValues") && item["typedValues"].is_object()) {
      for (auto it = item["typedValues"].begin(); it != item["typedValues"].end(); ++it) {
        t.typedValues[it.key()] = it.value().get<std::string>();
      }
    }
    if (!t.targetId.empty()) nativeTargets_[t.targetId] = std::move(t);
  }
}

void BrowserRelay::nativeSaveStateLocked() const {
  nlohmann::json targets = nlohmann::json::array();
  for (const auto& [id, t] : nativeTargets_) {
    nlohmann::json refs = nlohmann::json::object();
    for (const auto& [refId, ref] : t.refs) {
      refs[refId] = {{"role", ref.role}, {"name", ref.name}, {"text", ref.text}};
    }
    nlohmann::json typed = nlohmann::json::object();
    for (const auto& [refId, value] : t.typedValues) typed[refId] = value;
    targets.push_back({{"targetId", id},
                       {"url", t.url},
                       {"title", t.title},
                       {"html", t.html},
                       {"refs", refs},
                       {"typedValues", typed}});
  }

  std::ofstream out(nativeStatePath(), std::ios::trunc);
  if (!out) return;
  out << nlohmann::json{{"counter", nativeCounter_}, {"targets", targets}}.dump(2) << "\n";
}

nlohmann::json BrowserRelay::nativeStatus() const {
  std::lock_guard<std::mutex> lock(nativeMu_);
  nativeLoadStateLocked();
  return {{"ok", true},
          {"enabled", config_.enabled},
          {"backend", "native"},
          {"diagnosticMode", true},
          {"targets", nativeTargets_.size()},
          {"capabilities", {"status", "open", "navigate", "snapshot", "click", "type", "screenshot"}},
          {"limitations",
           {"Baseline local native backend without external openclaw CLI.",
            "Snapshot/interaction is deterministic diagnostic emulation, not full browser automation."}}};
}

nlohmann::json BrowserRelay::nativeOpen(const std::string& url) const {
  if (url.empty()) return {{"ok", false}, {"error", "url is required"}, {"code", "missing_url"}};

  NativeTarget t;
  {
    std::lock_guard<std::mutex> lock(nativeMu_);
    nativeLoadStateLocked();
    ++nativeCounter_;
    t.targetId = "native-" + std::to_string(nativeCounter_);
  }
  t.url = url;
  t.title = url;
  t.html = decodeDataHtml(url);

  if (url.find("example.com") != std::string::npos) {
    t.refs["e1"] = NativeRef{"link", "More information...", "More information..."};
  }
  if (!t.html.empty()) {
    t.refs["e1"] = NativeRef{"document", "document", ""};
    if (t.html.find("<a") != std::string::npos) t.refs["e2"] = NativeRef{"link", "link", ""};
    if (t.html.find("<input") != std::string::npos) t.refs["e3"] = NativeRef{"textbox", "q", ""};
    if (t.html.find("<textarea") != std::string::npos) t.refs["e4"] = NativeRef{"textbox", "textarea", ""};
  }

  {
    std::lock_guard<std::mutex> lock(nativeMu_);
    nativeLoadStateLocked();
    nativeTargets_[t.targetId] = t;
    nativeSaveStateLocked();
  }

  return {{"ok", true},
          {"backend", "native"},
          {"targetId", t.targetId},
          {"url", t.url},
          {"diagnostic", true}};
}

nlohmann::json BrowserRelay::nativeNavigate(const std::string& url, const std::string& targetId) const {
  if (url.empty()) return {{"ok", false}, {"error", "url is required"}, {"code", "missing_url"}};

  if (targetId.empty()) {
    auto opened = nativeOpen(url);
    opened["navigated"] = true;
    opened["createdTarget"] = true;
    return opened;
  }

  std::lock_guard<std::mutex> lock(nativeMu_);
  nativeLoadStateLocked();
  auto it = nativeTargets_.find(targetId);
  if (it == nativeTargets_.end()) {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "targetId not found"},
            {"code", "target_not_found"},
            {"targetId", targetId}};
  }

  it->second.url = url;
  it->second.title = url;
  it->second.html = decodeDataHtml(url);
  it->second.refs.clear();
  it->second.typedValues.clear();
  if (url.find("example.com") != std::string::npos) it->second.refs["e1"] = NativeRef{"link", "More information...", "More information..."};
  if (!it->second.html.empty()) {
    it->second.refs["e1"] = NativeRef{"document", "document", ""};
    if (it->second.html.find("<input") != std::string::npos) it->second.refs["e3"] = NativeRef{"textbox", "q", ""};
  }
  nativeSaveStateLocked();

  return {{"ok", true},
          {"backend", "native"},
          {"targetId", targetId},
          {"url", url},
          {"navigated", true},
          {"diagnostic", true}};
}

nlohmann::json BrowserRelay::nativeSnapshot(const std::string& urlHint, const std::string& targetId) const {
  std::string resolvedTargetId = targetId;
  if (!urlHint.empty()) {
    auto nav = nativeNavigate(urlHint, targetId);
    if (!nav.value("ok", false)) return nav;
    resolvedTargetId = nav.value("targetId", resolvedTargetId);
  }

  std::lock_guard<std::mutex> lock(nativeMu_);
  nativeLoadStateLocked();
  if (resolvedTargetId.empty()) {
    if (nativeTargets_.empty()) {
      return {{"ok", false}, {"backend", "native"}, {"error", "no target available"}, {"code", "target_required"}};
    }
    resolvedTargetId = nativeTargets_.rbegin()->first;
  }

  auto it = nativeTargets_.find(resolvedTargetId);
  if (it == nativeTargets_.end()) {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "targetId not found"},
            {"code", "target_not_found"},
            {"targetId", resolvedTargetId}};
  }

  nlohmann::json refs = nlohmann::json::object();
  for (const auto& [ref, info] : it->second.refs) {
    refs[ref] = {{"role", info.role}, {"name", info.name}, {"text", info.text}};
  }

  return {{"ok", true},
          {"backend", "native"},
          {"diagnostic", true},
          {"format", "ai"},
          {"targetId", resolvedTargetId},
          {"url", it->second.url},
          {"title", it->second.title},
          {"refs", refs},
          {"elements", refs.size()}};
}

nlohmann::json BrowserRelay::nativeClick(const std::string& ref, const std::string& targetId,
                                         bool doubleClick) const {
  std::lock_guard<std::mutex> lock(nativeMu_);
  nativeLoadStateLocked();
  auto it = nativeTargets_.find(targetId);
  if (targetId.empty() || it == nativeTargets_.end()) {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "targetId not found"},
            {"code", "target_not_found"},
            {"targetId", targetId}};
  }
  auto refIt = it->second.refs.find(ref);
  if (refIt == it->second.refs.end()) {
    return {{"ok", false}, {"backend", "native"}, {"error", "ref not found"}, {"code", "ref_not_found"}, {"ref", ref}};
  }

  return {{"ok", true},
          {"backend", "native"},
          {"diagnostic", true},
          {"targetId", targetId},
          {"ref", ref},
          {"double", doubleClick}};
}

nlohmann::json BrowserRelay::nativeType(const std::string& ref, const std::string& text,
                                        const std::string& targetId, bool submit,
                                        bool slowly) const {
  std::lock_guard<std::mutex> lock(nativeMu_);
  nativeLoadStateLocked();
  auto it = nativeTargets_.find(targetId);
  if (targetId.empty() || it == nativeTargets_.end()) {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "targetId not found"},
            {"code", "target_not_found"},
            {"targetId", targetId}};
  }
  auto refIt = it->second.refs.find(ref);
  if (refIt == it->second.refs.end()) {
    return {{"ok", false}, {"backend", "native"}, {"error", "ref not found"}, {"code", "ref_not_found"}, {"ref", ref}};
  }

  it->second.typedValues[ref] = text;
  nativeSaveStateLocked();
  return {{"ok", true},
          {"backend", "native"},
          {"diagnostic", true},
          {"targetId", targetId},
          {"ref", ref},
          {"textLength", text.size()},
          {"submit", submit},
          {"slowly", slowly}};
}

nlohmann::json BrowserRelay::nativeScreenshot(const std::string& targetId, bool fullPage,
                                              const std::string& type) const {
  const std::string ext = (type == "jpeg" || type == "jpg") ? "jpg" : "png";
  const auto out = std::filesystem::temp_directory_path() /
                   ("nexaclaw-native-shot-" + (targetId.empty() ? std::string("latest") : targetId) + "." + ext);
  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  f << "NexaClaw native screenshot placeholder\n";
  f << "targetId=" << targetId << "\n";
  f << "fullPage=" << (fullPage ? "true" : "false") << "\n";
  f.close();

  return {{"ok", true},
          {"backend", "native"},
          {"diagnostic", true},
          {"targetId", targetId},
          {"fullPage", fullPage},
          {"type", ext},
          {"path", out.string()}};
}

nlohmann::json BrowserRelay::status() const {
  if (useOpenClawCli()) {
    return runOpenClawBrowser({"status"});
  }
  if (useNativeBackend()) {
    return nativeStatus();
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
  if (useNativeBackend()) {
    return nativeOpen(url);
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
  if (useNativeBackend()) {
    return nativeSnapshot(urlHint, targetId);
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
  if (useNativeBackend()) {
    return nativeNavigate(url, targetId);
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
  if (useNativeBackend()) {
    return nativeClick(ref, targetId, doubleClick);
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
  if (useNativeBackend()) {
    return nativeType(ref, text, targetId, submit, slowly);
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
  if (useNativeBackend()) {
    return nativeScreenshot(targetId, fullPage, type);
  }

  return {{"ok", false},
          {"error", "screenshot is not implemented for backend: " + config_.backend},
          {"hint", "Use browser.backend=openclaw_cli"}};
}

}  // namespace clawforge::browser
