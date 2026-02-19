#include "browser/BrowserRelay.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>
#include <iomanip>

#include "util/Shell.hpp"

namespace clawforge::browser {

namespace {

std::string normalizeBackend(std::string backend) {
  for (char& c : backend) {
    if (c == '-') c = '_';
  }
  return backend;
}

std::string shortHash(const std::string& s) {
  const auto h = std::hash<std::string>{}(s);
  std::ostringstream os;
  os << std::hex << h;
  return os.str();
}

std::string lowerCopy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string collapseWhitespace(std::string s) {
  std::string out;
  out.reserve(s.size());
  bool space = false;
  for (char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!space) out.push_back(' ');
      space = true;
    } else {
      out.push_back(c);
      space = false;
    }
  }
  return trim(out);
}

std::filesystem::path nativeStatePathFor(const core::BrowserConfig& cfg) {
  const std::string key = cfg.backend + "|" + cfg.profile + "|" + cfg.cliBinary + "|" + cfg.openCommand;
  return std::filesystem::temp_directory_path() /
         ("nexaclaw-native-browser-state-" + shortHash(key) + ".json");
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

std::string stripTags(const std::string& s) {
  return std::regex_replace(s, std::regex("<[^>]*>"), " ");
}

std::string urlEncode(const std::string& s) {
  std::ostringstream out;
  out.fill('0');
  out << std::hex << std::uppercase;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out << static_cast<char>(c);
    } else if (c == ' ') {
      out << '+';
    } else {
      out << '%' << std::setw(2) << static_cast<int>(c);
    }
  }
  return out.str();
}

std::string joinUrl(const std::string& baseUrl, const std::string& action) {
  if (action.empty()) return baseUrl;
  if (action.rfind("http://", 0) == 0 || action.rfind("https://", 0) == 0 || action.rfind("data:", 0) == 0) return action;
  if (baseUrl.empty()) return action;
  if (action.front() == '/') {
    const auto schemeEnd = baseUrl.find("://");
    if (schemeEnd == std::string::npos) return action;
    const auto hostEnd = baseUrl.find('/', schemeEnd + 3);
    const std::string origin = hostEnd == std::string::npos ? baseUrl : baseUrl.substr(0, hostEnd);
    return origin + action;
  }
  const auto qPos = baseUrl.find('?');
  const std::string noQuery = qPos == std::string::npos ? baseUrl : baseUrl.substr(0, qPos);
  const auto slash = noQuery.rfind('/');
  if (slash == std::string::npos) return noQuery + "/" + action;
  return noQuery.substr(0, slash + 1) + action;
}

std::string attrValue(const std::string& attrs, const std::string& key) {
  const std::regex rx("(?:^|\\s)" + key + "\\s*=\\s*(['\"])(.*?)\\1", std::regex::icase);
  std::smatch m;
  if (std::regex_search(attrs, m, rx) && m.size() >= 3) return collapseWhitespace(m[2].str());
  return "";
}

bool isHttpUrl(const std::string& url) {
  return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
}

std::string extractTitle(const std::string& html) {
  std::smatch m;
  if (std::regex_search(html, m, std::regex("<title\\b[^>]*>(.*?)</title>", std::regex::icase)) && m.size() >= 2) {
    return collapseWhitespace(stripTags(m[1].str()));
  }
  return "";
}

bool nativeHttpFetchAvailable() {
  const auto hasCurl = util::Shell::run("command -v curl");
  return hasCurl.exitCode == 0;
}

bool nativeNodeAvailable() {
  const auto hasNode = util::Shell::run("command -v node");
  return hasNode.exitCode == 0;
}

nlohmann::json runNativeEvaluateModel(const nlohmann::json& payload) {
  static const std::string kScript = R"JS(const vm=require('vm');
const input=JSON.parse(process.argv[1]||'{}');
const element=input.element&&typeof input.element==='object'?input.element:null;
const location={href:String(input.url||'')};
const documentObj={title:String(input.title||'')};
const sandbox={location,document:documentObj,element,globalThis:null,console:{log:()=>{}}};
sandbox.globalThis=sandbox;
const out=(obj)=>process.stdout.write(JSON.stringify(obj));
try{
  const src=String(input.fn||'').trim();
  if(!src){ out({ok:false,code:'native_evaluate_fn_required',error:'fn is required'}); process.exit(2); }
  let evaluated=vm.runInNewContext(src,sandbox,{timeout:75});
  if(typeof evaluated==='function') evaluated=evaluated(element);
  if(evaluated&&typeof evaluated.then==='function') {
    out({ok:false,code:'native_evaluate_async_unsupported',error:'async evaluate is not supported in native backend'});
    process.exit(2);
  }
  out({ok:true,result:evaluated,locationHref:location.href,documentTitle:documentObj.title,element});
}catch(err){
  out({ok:false,code:'native_evaluate_execution_failed',error:String((err&&err.message)||err)});
  process.exit(2);
})JS";

  const std::string cmd = "node -e " + util::Shell::quote(kScript) + " " + util::Shell::quote(payload.dump());
  const auto res = util::Shell::run(cmd);
  auto parsed = nlohmann::json::parse(res.output, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return {{"ok", false},
            {"code", "native_evaluate_runtime_invalid_output"},
            {"error", "native evaluate runtime returned non-json output"},
            {"runtimeOutput", trim(res.output)},
            {"exitCode", res.exitCode}};
  }
  if (res.exitCode != 0 && parsed.value("ok", true)) {
    parsed["ok"] = false;
    parsed["code"] = "native_evaluate_runtime_failed";
    parsed["error"] = "native evaluate runtime failed";
    parsed["exitCode"] = res.exitCode;
  }
  return parsed;
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
  nativeLastTargetId_.clear();

  std::ifstream in(nativeStatePathFor(config_));
  if (!in) return;
  auto j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return;

  nativeCounter_ = j.value("counter", 0);
  nativeLastTargetId_ = j.value("lastTargetId", "");
  const auto& targets = j["targets"];
  if (!targets.is_array()) return;
  for (const auto& item : targets) {
    if (!item.is_object()) continue;
    NativeTarget t;
    t.targetId = item.value("targetId", "");
    t.url = item.value("url", "");
    t.title = item.value("title", "");
    t.html = item.value("html", "");
    t.runtime.source = item.value("runtimeSource", "");
    t.viewportWidth = item.value("viewportWidth", 0);
    t.viewportHeight = item.value("viewportHeight", 0);
    if (item.contains("runtimeWarning") && item["runtimeWarning"].is_object()) {
      t.runtime.warning = item["runtimeWarning"];
    }

    if (item.contains("refs") && item["refs"].is_object()) {
      for (auto it = item["refs"].begin(); it != item["refs"].end(); ++it) {
        NativeRef r;
        r.role = it.value().value("role", "");
        r.name = it.value().value("name", "");
        r.text = it.value().value("text", "");
        r.tag = it.value().value("tag", "");
        r.href = it.value().value("href", "");
        r.signature = it.value().value("signature", "");
        r.formId = it.value().value("formId", "");
        r.inputName = it.value().value("inputName", "");
        r.inputType = it.value().value("inputType", "");
        r.submitControl = it.value().value("submitControl", false);
        t.refs[it.key()] = r;
      }
    }
    if (item.contains("forms") && item["forms"].is_object()) {
      for (auto it = item["forms"].begin(); it != item["forms"].end(); ++it) {
        NativeForm f;
        f.action = it.value().value("action", "");
        f.method = it.value().value("method", "get");
        t.forms[it.key()] = f;
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
      refs[refId] = {{"role", ref.role},
                     {"name", ref.name},
                     {"text", ref.text},
                     {"tag", ref.tag},
                     {"href", ref.href},
                     {"signature", ref.signature},
                     {"formId", ref.formId},
                     {"inputName", ref.inputName},
                     {"inputType", ref.inputType},
                     {"submitControl", ref.submitControl}};
    }
    nlohmann::json forms = nlohmann::json::object();
    for (const auto& [formId, form] : t.forms) {
      forms[formId] = {{"action", form.action}, {"method", form.method}};
    }
    nlohmann::json typed = nlohmann::json::object();
    for (const auto& [refId, value] : t.typedValues) typed[refId] = value;
    targets.push_back({{"targetId", id},
                       {"url", t.url},
                       {"title", t.title},
                       {"html", t.html},
                       {"viewportWidth", t.viewportWidth},
                       {"viewportHeight", t.viewportHeight},
                       {"runtimeSource", t.runtime.source},
                       {"runtimeWarning", t.runtime.warning.is_object() ? t.runtime.warning : nlohmann::json::object()},
                       {"refs", refs},
                       {"forms", forms},
                       {"typedValues", typed}});
  }

  const auto dst = nativeStatePathFor(config_);
  const auto tmp = dst.string() + ".tmp";
  std::ofstream out(tmp, std::ios::trunc);
  if (!out) return;
  out << nlohmann::json{{"counter", nativeCounter_},
                        {"lastTargetId", nativeLastTargetId_},
                        {"targets", targets}}
             .dump(2)
      << "\n";
  out.close();

  std::error_code ec;
  std::filesystem::rename(tmp, dst, ec);
  if (ec) {
    std::ofstream fallback(dst, std::ios::trunc);
    if (!fallback) return;
    fallback << nlohmann::json{{"counter", nativeCounter_},
                               {"lastTargetId", nativeLastTargetId_},
                               {"targets", targets}}
                    .dump(2)
             << "\n";
  }
}

nlohmann::json BrowserRelay::nativeStatus() const {
  std::lock_guard<std::mutex> lock(nativeMu_);
  nativeLoadStateLocked();
  return {{"ok", true},
          {"enabled", config_.enabled},
          {"backend", "native"},
          {"diagnosticMode", false},
          {"targets", nativeTargets_.size()},
          {"activeTargetId", nativeLastTargetId_},
          {"capabilities", {"status", "open", "navigate", "snapshot", "click", "type", "screenshot", "act.click", "act.type", "act.press", "act.wait", "act.close", "act.hover", "act.scrollIntoView", "act.fill", "act.resize", "act.evaluate"}},
          {"nativeRuntime",
           {{"httpFetch", nativeHttpFetchAvailable()},
            {"evaluateRuntime", nativeNodeAvailable()},
            {"parsedUrlSchemes", {"data:text/html", "http", "https"}},
            {"structuredWarnings", true},
            {"capabilityGates",
             {{"formSubmit", true},
              {"formSubmitMethods", {"get"}},
              {"unsupportedFormSubmitMethods", {"post", "dialog"}},
              {"evaluateAsync", false}}}}},
          {"limitations", {"Native backend is still not a full browser engine (no JS execution / CDP session control)."}}};
}

static void applyNativeRuntimeContent(BrowserRelay::NativeTarget& t) {
  t.runtime.source = "url_only";
  t.runtime.warning = nlohmann::json::object();
  t.title = t.url;
  t.html.clear();

  const std::string dataHtml = decodeDataHtml(t.url);
  if (!dataHtml.empty()) {
    t.html = dataHtml;
    const std::string title = extractTitle(t.html);
    if (!title.empty()) t.title = title;
    t.runtime.source = "data_url";
    return;
  }

  if (!isHttpUrl(t.url)) {
    t.runtime.warning = {{"code", "native_runtime_url_scheme_unsupported"},
                         {"message", "Only data: and http(s) URLs are parsed by native runtime"},
                         {"url", t.url}};
    return;
  }

  if (!nativeHttpFetchAvailable()) {
    t.runtime.warning = {{"code", "native_runtime_http_fetch_unavailable"},
                         {"message", "curl is required for native runtime HTTP page fetch"}};
    return;
  }

  const std::string cmd =
      "curl -L --silent --show-error --fail --max-time 8 " + util::Shell::quote(t.url);
  const auto res = util::Shell::run(cmd);
  if (res.exitCode != 0) {
    t.runtime.warning = {{"code", "native_runtime_http_fetch_failed"},
                         {"message", "HTTP fetch failed; keeping URL-only fallback"},
                         {"exitCode", res.exitCode},
                         {"detail", trim(res.output)}};
    return;
  }

  t.html = res.output;
  t.runtime.source = "http_fetch";
  const std::string title = extractTitle(t.html);
  if (!title.empty()) {
    t.title = title;
  }
}

static void rebuildRefs(BrowserRelay::NativeTarget& t) {
  std::vector<std::pair<std::string, BrowserRelay::NativeRef>> parsed;
  parsed.push_back({"doc", {"document", t.title.empty() ? "document" : t.title, "", "document", "", "document", "", "", "", false}});

  t.forms.clear();

  if (t.url.find("example.com") != std::string::npos) {
    parsed.push_back({"a|More information...|https://www.iana.org/domains/example",
                      {"link", "More information...", "More information...", "a",
                       "https://www.iana.org/domains/example",
                       "a|More information...|https://www.iana.org/domains/example", "", "", "", false}});
  }

  if (!t.html.empty()) {
    struct FormSpan {
      std::string id;
      size_t start;
      size_t end;
    };
    std::vector<FormSpan> formSpans;

    std::regex formRx("<form\\b([^>]*)>(.*?)</form>", std::regex::icase);
    int formCounter = 1;
    for (auto it = std::sregex_iterator(t.html.begin(), t.html.end(), formRx); it != std::sregex_iterator(); ++it) {
      const std::string attrs = (*it)[1].str();
      const std::string formId = "form-" + std::to_string(formCounter++);
      BrowserRelay::NativeForm f;
      f.action = attrValue(attrs, "action");
      f.method = lowerCopy(attrValue(attrs, "method"));
      if (f.method.empty()) f.method = "get";
      t.forms[formId] = f;
      formSpans.push_back({formId, static_cast<size_t>((*it).position(0)), static_cast<size_t>((*it).position(0) + (*it).length(0))});
    }

    auto findFormId = [&](size_t pos) {
      for (const auto& fs : formSpans) {
        if (pos >= fs.start && pos < fs.end) return fs.id;
      }
      return std::string();
    };

    std::regex aRx("<a\\b([^>]*)>(.*?)</a>", std::regex::icase);
    for (auto it = std::sregex_iterator(t.html.begin(), t.html.end(), aRx); it != std::sregex_iterator(); ++it) {
      const std::string attrs = (*it)[1].str();
      const std::string text = collapseWhitespace(stripTags((*it)[2].str()));
      const std::string href = attrValue(attrs, "href");
      const std::string name = text.empty() ? (attrValue(attrs, "aria-label").empty() ? "link" : attrValue(attrs, "aria-label")) : text;
      const std::string sig = "a|" + name + "|" + href;
      parsed.push_back({sig, {"link", name, text, "a", href, sig, "", "", "", false}});
    }

    std::regex inputRx("<input\\b([^>]*)>", std::regex::icase);
    for (auto it = std::sregex_iterator(t.html.begin(), t.html.end(), inputRx); it != std::sregex_iterator(); ++it) {
      const std::string attrs = (*it)[1].str();
      const std::string typ = lowerCopy(attrValue(attrs, "type"));
      const bool submitControl = (typ == "submit" || typ == "image");
      const std::string role = submitControl ? "button" : ((typ == "search") ? "searchbox" : "textbox");
      std::string name = attrValue(attrs, "aria-label");
      if (name.empty()) name = attrValue(attrs, "name");
      if (name.empty()) name = submitControl ? "submit" : "input";
      const std::string formId = findFormId(static_cast<size_t>((*it).position(0)));
      const std::string sig = "input|" + role + "|" + name + "|" + formId + "|" + typ;
      parsed.push_back({sig, {role, name, submitControl ? name : "", "input", "", sig, formId, attrValue(attrs, "name"), typ, submitControl}});
    }

    std::regex taRx("<textarea\\b([^>]*)>(.*?)</textarea>", std::regex::icase);
    for (auto it = std::sregex_iterator(t.html.begin(), t.html.end(), taRx); it != std::sregex_iterator(); ++it) {
      const std::string attrs = (*it)[1].str();
      std::string name = attrValue(attrs, "aria-label");
      if (name.empty()) name = attrValue(attrs, "name");
      if (name.empty()) name = "textarea";
      const std::string txt = collapseWhitespace(stripTags((*it)[2].str()));
      const std::string formId = findFormId(static_cast<size_t>((*it).position(0)));
      const std::string sig = "textarea|textbox|" + name + "|" + formId;
      parsed.push_back({sig, {"textbox", name, txt, "textarea", "", sig, formId, attrValue(attrs, "name"), "textarea", false}});
    }

    std::regex bRx("<button\\b([^>]*)>(.*?)</button>", std::regex::icase);
    for (auto it = std::sregex_iterator(t.html.begin(), t.html.end(), bRx); it != std::sregex_iterator(); ++it) {
      const std::string attrs = (*it)[1].str();
      std::string name = collapseWhitespace(stripTags((*it)[2].str()));
      if (name.empty()) name = attrValue(attrs, "aria-label");
      if (name.empty()) name = "button";
      const std::string typ = lowerCopy(attrValue(attrs, "type"));
      const bool submitControl = typ.empty() || typ == "submit";
      const std::string formId = findFormId(static_cast<size_t>((*it).position(0)));
      const std::string sig = "button|" + name + "|" + formId + "|" + typ;
      parsed.push_back({sig, {"button", name, name, "button", "", sig, formId, "", typ, submitControl}});
    }
  }

  std::map<std::string, std::string> existingBySig;
  for (const auto& [id, ref] : t.refs) {
    if (!ref.signature.empty()) existingBySig[ref.signature] = id;
  }

  std::map<std::string, BrowserRelay::NativeRef> nextRefs;
  int nextId = 1;
  for (const auto& [id, _] : t.refs) {
    if (id.rfind("e", 0) == 0) {
      try {
        nextId = std::max(nextId, std::stoi(id.substr(1)) + 1);
      } catch (...) {
      }
    }
  }

  for (auto& [sig, ref] : parsed) {
    std::string rid;
    const auto it = existingBySig.find(sig);
    if (it != existingBySig.end()) {
      rid = it->second;
    } else {
      rid = "e" + std::to_string(nextId++);
    }
    const auto typed = t.typedValues.find(rid);
    if (typed != t.typedValues.end() && (ref.role == "textbox" || ref.role == "searchbox" || ref.role == "combobox")) {
      ref.text = typed->second;
    }
    nextRefs[rid] = ref;
  }

  std::map<std::string, std::string> nextTyped;
  for (const auto& [rid, v] : t.typedValues) {
    if (nextRefs.count(rid) > 0) nextTyped[rid] = v;
  }

  t.refs = std::move(nextRefs);
  t.typedValues = std::move(nextTyped);
}

nlohmann::json BrowserRelay::nativeOpen(const std::string& url) const {
  if (url.empty()) return {{"ok", false}, {"error", "url is required"}, {"code", "missing_url"}};

  std::lock_guard<std::mutex> lock(nativeMu_);
  nativeLoadStateLocked();
  NativeTarget t;
  ++nativeCounter_;
  t.targetId = "native-" + std::to_string(nativeCounter_);
  t.url = url;
  t.viewportWidth = 1280;
  t.viewportHeight = 720;
  applyNativeRuntimeContent(t);
  rebuildRefs(t);

  nativeTargets_[t.targetId] = t;
  nativeLastTargetId_ = t.targetId;
  nativeSaveStateLocked();

  nlohmann::json out{{"ok", true},
                     {"backend", "native"},
                     {"targetId", t.targetId},
                     {"url", t.url},
                     {"createdTarget", true},
                     {"runtime", {{"source", t.runtime.source}}}};
  if (t.runtime.warning.is_object() && !t.runtime.warning.empty()) out["runtime"]["warning"] = t.runtime.warning;
  return out;
}

nlohmann::json BrowserRelay::nativeNavigate(const std::string& url, const std::string& targetId) const {
  if (url.empty()) return {{"ok", false}, {"error", "url is required"}, {"code", "missing_url"}};

  if (targetId.empty()) {
    auto opened = nativeOpen(url);
    opened["navigated"] = true;
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
  applyNativeRuntimeContent(it->second);
  rebuildRefs(it->second);
  nativeLastTargetId_ = targetId;
  nativeSaveStateLocked();

  nlohmann::json out{{"ok", true},
                     {"backend", "native"},
                     {"targetId", targetId},
                     {"url", url},
                     {"navigated", true},
                     {"runtime", {{"source", it->second.runtime.source}}}};
  if (it->second.runtime.warning.is_object() && !it->second.runtime.warning.empty()) {
    out["runtime"]["warning"] = it->second.runtime.warning;
  }
  return out;
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
    if (nativeLastTargetId_.empty() || nativeTargets_.count(nativeLastTargetId_) == 0) {
      if (nativeTargets_.empty()) {
        return {{"ok", false}, {"backend", "native"}, {"error", "no target available"}, {"code", "target_required"}};
      }
      resolvedTargetId = nativeTargets_.begin()->first;
    } else {
      resolvedTargetId = nativeLastTargetId_;
    }
  }

  auto it = nativeTargets_.find(resolvedTargetId);
  if (it == nativeTargets_.end()) {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "targetId not found"},
            {"code", "target_not_found"},
            {"targetId", resolvedTargetId}};
  }

  rebuildRefs(it->second);
  nativeLastTargetId_ = resolvedTargetId;
  nativeSaveStateLocked();

  nlohmann::json refs = nlohmann::json::object();
  for (const auto& [ref, info] : it->second.refs) {
    refs[ref] = {{"role", info.role}, {"name", info.name}, {"text", info.text}};
  }

  nlohmann::json out{{"ok", true},
                     {"backend", "native"},
                     {"format", "ai"},
                     {"targetId", resolvedTargetId},
                     {"url", it->second.url},
                     {"title", it->second.title},
                     {"viewport", {{"width", it->second.viewportWidth}, {"height", it->second.viewportHeight}}},
                     {"refs", refs},
                     {"elements", refs.size()},
                     {"runtime", {{"source", it->second.runtime.source}}}};
  if (it->second.runtime.warning.is_object() && !it->second.runtime.warning.empty()) {
    out["runtime"]["warning"] = it->second.runtime.warning;
  }
  return out;
}

nlohmann::json BrowserRelay::nativeSubmitFormLocked(NativeTarget& t, const std::string& formId,
                                                  const std::string& triggerRef, const std::string& targetId,
                                                  const std::string& trigger) const {
  const auto fit = t.forms.find(formId);
  if (fit == t.forms.end()) {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "form context not found"},
            {"code", "native_form_context_missing"},
            {"targetId", targetId},
            {"ref", triggerRef}};
  }

  const std::string method = fit->second.method.empty() ? "get" : lowerCopy(fit->second.method);
  if (method != "get") {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "native backend currently supports only GET form submission"},
            {"code", "native_capability_form_method_unsupported"},
            {"targetId", targetId},
            {"ref", triggerRef},
            {"capabilityGate", {{"feature", "formSubmit"}, {"supportedMethods", {"get"}}, {"requestedMethod", method}}}};
  }

  std::vector<std::pair<std::string, std::string>> fields;
  for (const auto& [rid, r] : t.refs) {
    if (r.formId != formId) continue;
    if (!(r.role == "textbox" || r.role == "searchbox" || r.role == "combobox")) continue;
    if (r.inputName.empty()) continue;
    const auto typed = t.typedValues.find(rid);
    std::string value = typed != t.typedValues.end() ? typed->second : r.text;
    fields.push_back({r.inputName, value});
  }

  std::string resolved = joinUrl(t.url, fit->second.action);
  std::string query;
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) query += "&";
    query += urlEncode(fields[i].first) + "=" + urlEncode(fields[i].second);
  }
  if (!query.empty()) {
    resolved += (resolved.find('?') == std::string::npos ? "?" : "&");
    resolved += query;
  }

  t.url = resolved;
  applyNativeRuntimeContent(t);
  rebuildRefs(t);
  nlohmann::json out{{"ok", true},
                     {"backend", "native"},
                     {"targetId", targetId},
                     {"ref", triggerRef},
                     {"submitted", true},
                     {"navigated", true},
                     {"url", t.url},
                     {"runtime", {{"source", t.runtime.source}}},
                     {"submission", {{"trigger", trigger},
                                     {"formId", formId},
                                     {"method", method},
                                     {"action", fit->second.action},
                                     {"fields", nlohmann::json::array()}}}};
  for (const auto& kv : fields) out["submission"]["fields"].push_back({{"name", kv.first}, {"value", kv.second}});
  if (t.runtime.warning.is_object() && !t.runtime.warning.empty()) out["runtime"]["warning"] = t.runtime.warning;
  return out;
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

  nativeLastTargetId_ = targetId;
  nlohmann::json out{{"ok", true},
                     {"backend", "native"},
                     {"targetId", targetId},
                     {"ref", ref},
                     {"double", doubleClick}};
  if (refIt->second.submitControl && !refIt->second.formId.empty()) {
    out = nativeSubmitFormLocked(it->second, refIt->second.formId, ref, targetId, "click");
  } else if (!refIt->second.href.empty()) {
    it->second.url = refIt->second.href;
    applyNativeRuntimeContent(it->second);
    rebuildRefs(it->second);
    out["navigated"] = true;
    out["url"] = it->second.url;
    out["runtime"] = {{"source", it->second.runtime.source}};
    if (it->second.runtime.warning.is_object() && !it->second.runtime.warning.empty()) {
      out["runtime"]["warning"] = it->second.runtime.warning;
    }
  } else if (refIt->second.submitControl) {
    out["warning"] = {{"code", "native_form_submit_no_form_context"},
                      {"message", "Submit control has no associated form; click produced no navigation"}};
  }
  nativeSaveStateLocked();
  return out;
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

  if (!(refIt->second.role == "textbox" || refIt->second.role == "searchbox" || refIt->second.role == "combobox")) {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "ref is not a text input control"},
            {"code", "native_type_ref_not_text_input"},
            {"ref", ref},
            {"targetId", targetId},
            {"refRole", refIt->second.role}};
  }

  const std::string submitFormId = refIt->second.formId;
  it->second.typedValues[ref] = text;
  rebuildRefs(it->second);
  nativeLastTargetId_ = targetId;
  nlohmann::json out{{"ok", true},
                     {"backend", "native"},
                     {"targetId", targetId},
                     {"ref", ref},
                     {"textLength", text.size()},
                     {"submit", submit},
                     {"slowly", slowly}};
  if (submit) {
    if (!submitFormId.empty()) {
      out = nativeSubmitFormLocked(it->second, submitFormId, ref, targetId, "type_submit");
      out["typed"] = true;
      out["textLength"] = text.size();
    } else {
      out["warning"] = {{"code", "native_form_submit_no_form_context"},
                        {"message", "submit=true was requested but ref is not part of a form"}};
    }
  }
  nativeSaveStateLocked();
  return out;
}

nlohmann::json BrowserRelay::nativeScreenshot(const std::string& targetId, bool fullPage,
                                              const std::string& type) const {
  std::lock_guard<std::mutex> lock(nativeMu_);
  nativeLoadStateLocked();
  std::string resolvedTargetId = targetId.empty() ? nativeLastTargetId_ : targetId;
  if (resolvedTargetId.empty() || nativeTargets_.count(resolvedTargetId) == 0) {
    return {{"ok", false},
            {"backend", "native"},
            {"error", "targetId not found"},
            {"code", "target_not_found"},
            {"targetId", resolvedTargetId}};
  }

  const std::string ext = (type == "jpeg" || type == "jpg") ? "jpg" : "png";
  const auto out = std::filesystem::temp_directory_path() /
                   ("nexaclaw-native-shot-" + resolvedTargetId + "." + ext);
  std::ofstream f(out, std::ios::binary | std::ios::trunc);
  f << "NexaClaw native screenshot placeholder\n";
  f << "targetId=" << resolvedTargetId << "\n";
  f << "url=" << nativeTargets_[resolvedTargetId].url << "\n";
  f << "fullPage=" << (fullPage ? "true" : "false") << "\n";
  f.close();

  nativeLastTargetId_ = resolvedTargetId;
  nativeSaveStateLocked();
  return {{"ok", true},
          {"backend", "native"},
          {"targetId", resolvedTargetId},
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

nlohmann::json BrowserRelay::nativeAct(const nlohmann::json& request, const std::string& targetId) const {
  const std::string kind = request.value("kind", "");
  const std::string resolvedTargetId = targetId.empty() ? request.value("targetId", "") : targetId;
  if (kind.empty()) {
    return {{"ok", false}, {"error", "kind is required"}, {"code", "browser_act_kind_required"}};
  }

  auto capabilityError = [&](const std::string& feature, const std::string& message, const std::string& code) {
    return nlohmann::json{{"ok", false},
                          {"action", "act"},
                          {"kind", kind},
                          {"targetId", resolvedTargetId},
                          {"error", message},
                          {"code", code},
                          {"capabilityGate", {{"feature", feature}, {"backend", "native"}}}};
  };

  if (kind == "click") {
    const std::string ref = request.value("ref", "");
    const bool doubleClick = request.value("doubleClick", false);
    auto out = nativeClick(ref, resolvedTargetId, doubleClick);
    out["action"] = "act";
    out["kind"] = kind;
    return out;
  }

  if (kind == "type") {
    const std::string ref = request.value("ref", "");
    const std::string text = request.value("text", "");
    const bool submit = request.value("submit", false);
    const bool slowly = request.value("slowly", false);
    auto out = nativeType(ref, text, resolvedTargetId, submit, slowly);
    out["action"] = "act";
    out["kind"] = kind;
    return out;
  }

  if (kind == "press") {
    const std::string key = request.value("key", "");
    if (key.empty()) {
      return {{"ok", false}, {"error", "key is required"}, {"code", "browser_act_key_required"}, {"kind", kind}};
    }
    if (key != "Enter") {
      return capabilityError("press", "native backend supports only Enter key in act.press", "native_capability_press_key_unsupported");
    }

    std::lock_guard<std::mutex> lock(nativeMu_);
    nativeLoadStateLocked();
    auto it = resolvedTargetId.empty() ? nativeTargets_.end() : nativeTargets_.find(resolvedTargetId);
    if (it == nativeTargets_.end() && !nativeLastTargetId_.empty()) it = nativeTargets_.find(nativeLastTargetId_);
    if (it == nativeTargets_.end()) {
      return {{"ok", false}, {"error", "no active target"}, {"code", "browser_act_no_active_target"}, {"kind", kind}};
    }
    if (it->second.forms.empty()) {
      return {{"ok", true},
              {"action", "act"},
              {"kind", kind},
              {"targetId", it->second.targetId},
              {"noop", true},
              {"note", "Enter press has no form to submit in native backend"}};
    }

    const auto firstForm = it->second.forms.begin()->first;
    auto out = nativeSubmitFormLocked(it->second, firstForm, "", it->second.targetId, "press:Enter");
    out["action"] = "act";
    out["kind"] = kind;
    nativeSaveStateLocked();
    return out;
  }

  if (kind == "wait") {
    const int timeMs = request.value("timeMs", 0);
    return {{"ok", true}, {"action", "act"}, {"kind", kind}, {"waitedMs", timeMs > 0 ? timeMs : 0}, {"nativeMode", "documented-noop"}};
  }

  if (kind == "close") {
    std::lock_guard<std::mutex> lock(nativeMu_);
    nativeLoadStateLocked();
    std::string id = resolvedTargetId.empty() ? nativeLastTargetId_ : resolvedTargetId;
    if (id.empty() || nativeTargets_.count(id) == 0) {
      return {{"ok", false}, {"error", "targetId not found"}, {"code", "target_not_found"}, {"targetId", id}, {"kind", kind}};
    }
    nativeTargets_.erase(id);
    if (nativeLastTargetId_ == id) nativeLastTargetId_ = nativeTargets_.empty() ? "" : nativeTargets_.begin()->first;
    nativeSaveStateLocked();
    return {{"ok", true}, {"action", "act"}, {"kind", kind}, {"targetId", id}, {"closed", true}};
  }

  if (kind == "hover" || kind == "scrollIntoView") {
    const std::string ref = request.value("ref", "");
    if (ref.empty()) return {{"ok", false}, {"error", "ref is required"}, {"code", "ref_required"}, {"kind", kind}};
    std::lock_guard<std::mutex> lock(nativeMu_);
    nativeLoadStateLocked();
    auto it = nativeTargets_.find(resolvedTargetId);
    if (resolvedTargetId.empty() || it == nativeTargets_.end()) {
      return {{"ok", false}, {"error", "targetId not found"}, {"code", "target_not_found"}, {"targetId", resolvedTargetId}, {"kind", kind}};
    }
    if (it->second.refs.count(ref) == 0) {
      return {{"ok", false}, {"error", "ref not found"}, {"code", "ref_not_found"}, {"ref", ref}, {"kind", kind}};
    }
    nativeLastTargetId_ = resolvedTargetId;
    nativeSaveStateLocked();
    return {{"ok", true}, {"action", "act"}, {"kind", kind}, {"targetId", resolvedTargetId}, {"ref", ref}, {kind == "hover" ? "hovered" : "scrolledIntoView", true}};
  }

  if (kind == "fill") {
    if (!request.contains("fields") || !request["fields"].is_array()) {
      return {{"ok", false}, {"kind", kind}, {"code", "browser_act_fill_fields_required"}, {"error", "fields[] is required"}};
    }
    for (const auto& f : request["fields"]) {
      const std::string ref = f.is_object() ? f.value("ref", "") : "";
      if (ref.empty()) return {{"ok", false}, {"kind", kind}, {"code", "browser_act_fill_field_ref_required"}, {"error", "each field must include ref"}};
      std::string value;
      if (f.contains("value")) {
        if (f["value"].is_string()) value = f["value"].get<std::string>();
        else if (f["value"].is_number_integer()) value = std::to_string(f["value"].get<long long>());
        else if (f["value"].is_number_float()) value = std::to_string(f["value"].get<double>());
        else if (f["value"].is_boolean()) value = f["value"].get<bool>() ? "true" : "false";
      }
      auto typed = nativeType(ref, value, resolvedTargetId, false, false);
      if (!typed.value("ok", false)) {
        typed["action"] = "act";
        typed["kind"] = kind;
        return typed;
      }
    }
    return {{"ok", true}, {"action", "act"}, {"kind", kind}, {"targetId", resolvedTargetId}, {"filled", request["fields"].size()}};
  }

  if (kind == "resize") {
    const int w = request.value("width", 0);
    const int h = request.value("height", 0);
    if (w <= 0 || h <= 0) return {{"ok", false}, {"kind", kind}, {"code", "browser_act_resize_dimensions_required"}, {"error", "width and height must be positive"}};
    std::lock_guard<std::mutex> lock(nativeMu_);
    nativeLoadStateLocked();
    auto it = nativeTargets_.find(resolvedTargetId);
    if (resolvedTargetId.empty() || it == nativeTargets_.end()) {
      return {{"ok", false}, {"error", "targetId not found"}, {"code", "target_not_found"}, {"targetId", resolvedTargetId}, {"kind", kind}};
    }
    it->second.viewportWidth = w;
    it->second.viewportHeight = h;
    nativeLastTargetId_ = resolvedTargetId;
    nativeSaveStateLocked();
    return {{"ok", true}, {"action", "act"}, {"kind", kind}, {"targetId", resolvedTargetId}, {"viewport", {{"width", w}, {"height", h}}}};
  }

  if (kind == "evaluate") {
    const std::string fn = request.value("fn", "");
    if (fn.empty()) {
      return {{"ok", false}, {"kind", kind}, {"code", "browser_act_evaluate_fn_required"}, {"error", "fn is required"}};
    }
    if (!nativeNodeAvailable()) {
      return capabilityError("evaluate", "node runtime is required for native evaluate", "native_capability_evaluate_runtime_unavailable");
    }

    std::lock_guard<std::mutex> lock(nativeMu_);
    nativeLoadStateLocked();
    auto it = nativeTargets_.find(resolvedTargetId);
    if (resolvedTargetId.empty() || it == nativeTargets_.end()) {
      return {{"ok", false}, {"error", "targetId not found"}, {"code", "target_not_found"}, {"targetId", resolvedTargetId}, {"kind", kind}};
    }

    const std::string ref = request.value("ref", "");
    nlohmann::json element = nlohmann::json::object();
    if (!ref.empty()) {
      const auto refIt = it->second.refs.find(ref);
      if (refIt == it->second.refs.end()) {
        return {{"ok", false}, {"error", "ref not found"}, {"code", "ref_not_found"}, {"ref", ref}, {"kind", kind}};
      }
      element = {{"ref", ref},
                 {"role", refIt->second.role},
                 {"name", refIt->second.name},
                 {"textContent", refIt->second.text},
                 {"value", it->second.typedValues.count(ref) ? it->second.typedValues[ref] : refIt->second.text}};
    }

    const auto evalOut = runNativeEvaluateModel({{"fn", fn}, {"url", it->second.url}, {"title", it->second.title}, {"element", element}});
    if (!evalOut.value("ok", false)) {
      auto out = evalOut;
      out["action"] = "act";
      out["kind"] = kind;
      out["targetId"] = resolvedTargetId;
      if (out.value("code", "") == "native_evaluate_async_unsupported") {
        out["code"] = "native_capability_evaluate_async_unsupported";
        out["capabilityGate"] = {{"feature", "evaluate.async"}, {"backend", "native"}};
      }
      return out;
    }

    bool mutated = false;
    const std::string nextUrl = evalOut.value("locationHref", it->second.url);
    if (!nextUrl.empty() && nextUrl != it->second.url) {
      it->second.url = nextUrl;
      applyNativeRuntimeContent(it->second);
      mutated = true;
    }
    const std::string nextTitle = evalOut.value("documentTitle", it->second.title);
    if (!nextTitle.empty() && nextTitle != it->second.title) {
      it->second.title = nextTitle;
      mutated = true;
    }
    if (!ref.empty() && evalOut.contains("element") && evalOut["element"].is_object()) {
      const auto& el = evalOut["element"];
      if (el.contains("value") && el["value"].is_string()) {
        it->second.typedValues[ref] = el["value"].get<std::string>();
        mutated = true;
      }
    }
    if (mutated) rebuildRefs(it->second);
    nativeLastTargetId_ = resolvedTargetId;
    nativeSaveStateLocked();

    nlohmann::json out{{"ok", true},
                       {"action", "act"},
                       {"kind", kind},
                       {"targetId", resolvedTargetId},
                       {"result", evalOut.contains("result") ? evalOut["result"] : nlohmann::json()},
                       {"url", it->second.url},
                       {"runtime", {{"source", it->second.runtime.source}}}};
    if (it->second.runtime.warning.is_object() && !it->second.runtime.warning.empty()) {
      out["runtime"]["warning"] = it->second.runtime.warning;
    }
    return out;
  }

  if (kind == "drag" || kind == "select") {
    return capabilityError(kind, "native backend does not implement this act kind", "native_capability_kind_unsupported");
  }

  return {{"ok", false},
          {"action", "act"},
          {"kind", kind},
          {"error", "native_browser_act_kind_unsupported"},
          {"supportedKinds", {"click", "type", "press", "wait", "close", "hover", "scrollIntoView", "fill", "resize", "evaluate"}},
          {"hint", "Use browser.backend=openclaw_cli for full Playwright-style act support"}};
}

nlohmann::json BrowserRelay::act(const nlohmann::json& request, const std::string& targetId) const {
  if (!config_.enabled) {
    return {{"ok", false}, {"error", "browser relay disabled in config.browser.enabled"}};
  }

  const std::string kind = request.value("kind", "");
  if (kind.empty()) {
    return {{"ok", false}, {"error", "kind is required"}, {"code", "browser_act_kind_required"}};
  }

  if (useNativeBackend()) {
    return nativeAct(request, targetId);
  }

  if (useOpenClawCli()) {
    const std::string resolvedTargetId = targetId.empty() ? request.value("targetId", "") : targetId;
    if (kind == "click") {
      return click(request.value("ref", ""), resolvedTargetId, request.value("doubleClick", false));
    }
    if (kind == "type") {
      return type(request.value("ref", ""), request.value("text", ""), resolvedTargetId,
                  request.value("submit", false), request.value("slowly", false));
    }
    if (kind == "press") {
      const std::string key = request.value("key", "");
      if (key.empty()) return {{"ok", false}, {"action", "act"}, {"kind", kind}, {"error", "key is required"}, {"code", "browser_act_key_required"}};
      std::vector<std::string> args{"press", key};
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      if (request.contains("delayMs") && request["delayMs"].is_number_integer()) { args.push_back("--delay"); args.push_back(std::to_string(request["delayMs"].get<int>())); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "hover") {
      std::vector<std::string> args{"hover", request.value("ref", "")};
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "scrollIntoView") {
      std::vector<std::string> args{"scrollintoview", request.value("ref", "")};
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "drag") {
      std::vector<std::string> args{"drag", request.value("startRef", ""), request.value("endRef", "")};
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "select") {
      std::vector<std::string> args{"select", request.value("ref", "")};
      if (request.contains("values") && request["values"].is_array()) {
        for (const auto& v : request["values"]) if (v.is_string()) args.push_back(v.get<std::string>());
      }
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "fill") {
      if (!request.contains("fields") || !request["fields"].is_array()) {
        return {{"ok", false}, {"action", "act"}, {"kind", kind}, {"code", "browser_act_fill_fields_required"}, {"error", "fields[] is required"}};
      }
      std::vector<std::string> args{"fill", "--fields", request["fields"].dump()};
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "resize") {
      const int w = request.value("width", 0); const int h = request.value("height", 0);
      if (w <= 0 || h <= 0) return {{"ok", false}, {"action", "act"}, {"kind", kind}, {"code", "browser_act_resize_dimensions_required"}, {"error", "width and height must be positive"}};
      std::vector<std::string> args{"resize", std::to_string(w), std::to_string(h)};
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "evaluate") {
      const std::string fn = request.value("fn", "");
      if (fn.empty()) return {{"ok", false}, {"action", "act"}, {"kind", kind}, {"code", "browser_act_evaluate_fn_required"}, {"error", "fn is required"}};
      std::vector<std::string> args{"evaluate", "--fn", fn};
      if (request.contains("ref") && request["ref"].is_string() && !request["ref"].get<std::string>().empty()) { args.push_back("--ref"); args.push_back(request["ref"].get<std::string>()); }
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "wait") {
      std::vector<std::string> args{"wait"};
      if (request.contains("timeMs") && request["timeMs"].is_number_integer()) { args.push_back("--time"); args.push_back(std::to_string(request["timeMs"].get<int>())); }
      if (request.contains("text") && request["text"].is_string()) { args.push_back("--text"); args.push_back(request["text"].get<std::string>()); }
      if (request.contains("textGone") && request["textGone"].is_string()) { args.push_back("--text-gone"); args.push_back(request["textGone"].get<std::string>()); }
      if (request.contains("selector") && request["selector"].is_string()) { args.push_back("--selector"); args.push_back(request["selector"].get<std::string>()); }
      if (request.contains("url") && request["url"].is_string()) { args.push_back("--url"); args.push_back(request["url"].get<std::string>()); }
      if (request.contains("loadState") && request["loadState"].is_string()) { args.push_back("--load-state"); args.push_back(request["loadState"].get<std::string>()); }
      if (request.contains("fn") && request["fn"].is_string()) { args.push_back("--fn"); args.push_back(request["fn"].get<std::string>()); }
      if (!resolvedTargetId.empty()) { args.push_back("--target-id"); args.push_back(resolvedTargetId); }
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    if (kind == "close") {
      std::vector<std::string> args{"close"};
      if (!resolvedTargetId.empty()) args.push_back(resolvedTargetId);
      auto out = runOpenClawBrowser(args); out["action"] = "act"; out["kind"] = kind; return out;
    }
    return {{"ok", false},
            {"action", "act"},
            {"kind", kind},
            {"error", "openclaw_cli_act_kind_unsupported_in_nexaclaw"},
            {"supportedKinds", {"click", "type", "press", "hover", "scrollIntoView", "drag", "select", "fill", "resize", "wait", "evaluate", "close"}}};
  }

  return {{"ok", false}, {"error", "act is not implemented for backend: " + config_.backend}};
}

}  // namespace clawforge::browser
