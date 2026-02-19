#include "http/HttpServer.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/Logger.hpp"
#include "util/TimeUtil.hpp"

namespace clawforge::http {

using json = nlohmann::json;

namespace {

json parseBody(const httplib::Request& req) {
  if (req.body.empty()) return json::object();
  return json::parse(req.body, nullptr, false);
}

void replyJson(httplib::Response& res, const json& payload, int status = 200) {
  res.status = status;
  res.set_content(payload.dump(2), "application/json; charset=utf-8");
}

}  // namespace

HttpServer::HttpServer(std::string host, int port, agent::AgentEngine& agent,
                       session::SessionStore& sessions, tools::ToolRegistry& tools,
                       browser::BrowserRelay& browser, orchestration::TaskQueue& tasks,
                       automation::CronScheduler& cron, core::EventBus& events,
                       core::ApiConfig apiConfig, core::GatewayAuthConfig authConfig,
                       core::RateLimitConfig rateLimitConfig, core::AuditConfig auditConfig,
                       int64_t startedAtMs)
    : host_(std::move(host)),
      port_(port),
      agent_(agent),
      sessions_(sessions),
      tools_(tools),
      browser_(browser),
      tasks_(tasks),
      cron_(cron),
      events_(events),
      apiConfig_(std::move(apiConfig)),
      authConfig_(std::move(authConfig)),
      rateLimiter_(rateLimitConfig.enabled, rateLimitConfig.maxRequests, rateLimitConfig.windowMs),
      audit_(auditConfig.enabled, auditConfig.file),
      auditFilePath_(auditConfig.file),
      startedAtMs_(startedAtMs) {
  setupRoutes();
}

std::string HttpServer::deriveApiSessionKey(const nlohmann::json& body) const {
  const std::string explicitKey = body.value("sessionKey", "");
  if (!explicitKey.empty()) return explicitKey;
  const std::string peerId = body.value("peerId", "");
  const std::string channelId = body.value("channelId", "api");
  return agent_.deriveSessionKey(channelId, peerId);
}

int HttpServer::parsePositiveLimit(const httplib::Request& req, const std::string& key, int fallback, int maxValue) {
  if (!req.has_param(key)) return fallback;
  try {
    const int value = std::stoi(req.get_param_value(key));
    return std::max(1, std::min(value, maxValue));
  } catch (...) {
    return fallback;
  }
}

nlohmann::json HttpServer::readAuditTail(int limit) const {
  json rows = json::array();
  if (limit <= 0) return rows;

  std::ifstream in(auditFilePath_);
  if (!in) return rows;

  std::vector<std::string> buffer;
  buffer.reserve(static_cast<std::size_t>(limit));
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    buffer.push_back(line);
    if (static_cast<int>(buffer.size()) > limit) {
      buffer.erase(buffer.begin());
    }
  }

  for (const auto& raw : buffer) {
    auto parsed = json::parse(raw, nullptr, false);
    if (parsed.is_discarded()) {
      rows.push_back({{"raw", raw}});
    } else {
      rows.push_back(parsed);
    }
  }
  return rows;
}

nlohmann::json HttpServer::readEventTail(int limit) const {
  json rows = json::array();
  for (const auto& e : events_.recent(static_cast<std::size_t>(std::max(1, limit)))) {
    rows.push_back({{"id", e.id}, {"type", e.type}, {"ts", e.ts}, {"data", e.data}});
  }
  return rows;
}

void HttpServer::setupRoutes() {
  server_.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
    if (req.path.rfind("/api/", 0) != 0) return httplib::Server::HandlerResponse::Unhandled;

    const auto nowMs = util::TimeUtil::nowMillis();
    const std::string source = req.remote_addr.empty() ? "unknown" : req.remote_addr;
    if (!rateLimiter_.allow(source, nowMs)) {
      audit_.write("rate_limit_deny", {{"path", req.path}, {"source", source}});
      replyJson(res, {{"ok", false}, {"error", "Rate limit exceeded"}}, 429);
      return httplib::Server::HandlerResponse::Handled;
    }

    if (authConfig_.mode == "off") return httplib::Server::HandlerResponse::Unhandled;

    if (authConfig_.mode != "token") {
      replyJson(res, {{"ok", false}, {"error", "Invalid gateway.auth.mode: " + authConfig_.mode}}, 500);
      return httplib::Server::HandlerResponse::Handled;
    }

    const char* token = std::getenv(authConfig_.tokenEnv.c_str());
    const std::string expected = token ? std::string(token) : "";
    if (expected.empty()) {
      replyJson(res, {{"ok", false}, {"error", "gateway auth token env is empty: " + authConfig_.tokenEnv}}, 500);
      return httplib::Server::HandlerResponse::Handled;
    }

    const std::string authHeader = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    const bool ok = authHeader.rfind(prefix, 0) == 0 && authHeader.substr(prefix.size()) == expected;
    if (!ok) {
      audit_.write("auth_deny", {{"path", req.path}, {"source", source}});
      replyJson(res, {{"ok", false}, {"error", "Unauthorized: Bearer token required"}}, 401);
      return httplib::Server::HandlerResponse::Handled;
    }

    return httplib::Server::HandlerResponse::Unhandled;
  });

  server_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    replyJson(res, {{"ok", true}, {"service", "nexaclaw"}});
  });

  server_.Get("/admin", [](const httplib::Request&, httplib::Response& res) {
    static const char* kAdminHtml = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1" />
<title>NexaClaw Admin Console</title>
<style>
:root{--bg:#0f1115;--card:#171a21;--line:#2a3240;--fg:#e7edf7;--muted:#9aabc3;--ok:#2ecc71;--warn:#f1c40f;--bad:#ff6b6b;--accent:#6da8ff}
*{box-sizing:border-box} body{margin:0;background:var(--bg);color:var(--fg);font-family:Inter,system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
.container{max-width:1280px;margin:0 auto;padding:16px} h1,h2,h3{margin:0 0 8px}
.grid{display:grid;gap:12px}.g4{grid-template-columns:repeat(auto-fit,minmax(220px,1fr))}.g2{grid-template-columns:1.25fr 1fr}
.card{border:1px solid var(--line);border-radius:12px;background:var(--card);padding:12px;min-width:0}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.between{justify-content:space-between}
input,select,button{background:#10131a;color:var(--fg);border:1px solid var(--line);border-radius:8px;padding:7px 9px}
button{cursor:pointer} button:hover{border-color:var(--accent)} .muted{color:var(--muted);font-size:13px}
.kpi{font-size:26px;font-weight:700}.pill{padding:2px 8px;border-radius:999px;font-size:12px;border:1px solid var(--line)}
.ok{color:var(--ok)} .warn{color:var(--warn)} .bad{color:var(--bad)}
.table{width:100%;border-collapse:collapse;font-size:13px} .table th,.table td{padding:6px;border-bottom:1px solid #233}
.table th{text-align:left;color:var(--muted);font-weight:600} pre{margin:0;background:#0c0f14;border:1px solid #223;border-radius:10px;padding:10px;max-height:240px;overflow:auto;white-space:pre-wrap}
.small{font-size:12px}
</style>
</head><body><div class="container">
  <div class="row between">
    <div><h1>NexaClaw Admin Console</h1><div class="muted">Stage19 slice2 — operator dashboard (local/loopback oriented)</div></div>
    <div class="row"><span id="conn" class="pill">idle</span><span id="last" class="small muted"></span></div>
  </div>

  <div class="card">
    <div class="row">
      <label>Bearer token <input id="tok" type="password" placeholder="optional"></label>
      <button id="saveTok">Save token</button>
      <label>Auto-refresh <select id="refreshMs"><option value="0">off</option><option value="3000">3s</option><option value="5000">5s</option><option value="10000" selected>10s</option><option value="30000">30s</option></select></label>
      <button id="toggleAuto">Pause</button>
      <button id="refreshBtn">Refresh now</button>
    </div>
    <div class="muted">Safe controls only in UI (cron run due/enable/disable). Auth/rate-limit middleware still enforced on /api.</div>
  </div>

  <div class="grid g4">
    <div class="card"><div class="muted">Service</div><div id="kService" class="kpi">-</div><div id="kNow" class="small muted"></div></div>
    <div class="card"><div class="muted">Uptime</div><div id="kUptime" class="kpi">-</div><div id="kAuth" class="small muted"></div></div>
    <div class="card"><div class="muted">Sessions</div><div id="kSessions" class="kpi">0</div><div id="kSessionFresh" class="small muted"></div></div>
    <div class="card"><div class="muted">Cron jobs</div><div id="kCron" class="kpi">0</div><div id="kCronHealth" class="small muted"></div></div>
  </div>

  <div class="grid g2">
    <div class="card"><h3>Sessions (recent first)</h3><div id="sessionsTbl"></div></div>
    <div class="card"><h3>Cron quick controls</h3><div id="cronQuick" class="small"></div><pre id="cronRaw"></pre></div>
  </div>

  <div class="grid g2">
    <div class="card"><h3>Recent events</h3><pre id="logs"></pre></div>
    <div class="card"><h3>Audit tail</h3><pre id="audit"></pre></div>
  </div>

  <div class="card"><h3>Overview raw JSON</h3><pre id="overview"></pre></div>
</div>
<script>
const LS_TOKEN='nexaclaw_admin_token';
const LS_REFRESH='nexaclaw_admin_refresh_ms';
let timer=null, paused=false;
const $=id=>document.getElementById(id);
const getTok=()=>localStorage.getItem(LS_TOKEN)||'';
const hdrs=()=>{const h={'Content-Type':'application/json'}; const t=getTok(); if(t) h['Authorization']='Bearer '+t; return h;};
const fmt=(o)=>JSON.stringify(o,null,2);
const age=(iso)=>{if(!iso)return '-';const ms=Date.now()-Date.parse(iso);if(!Number.isFinite(ms))return iso;const s=Math.max(0,Math.floor(ms/1000));if(s<60)return s+'s ago';if(s<3600)return Math.floor(s/60)+'m ago';if(s<86400)return Math.floor(s/3600)+'h ago';return Math.floor(s/86400)+'d ago';};
const msTo=(v)=>{v=Number(v||0);if(v<1000)return v+'ms';const s=Math.floor(v/1000);if(s<60)return s+'s';const m=Math.floor(s/60);if(m<60)return m+'m';const h=Math.floor(m/60);return h+'h';};
async function jget(url){const r=await fetch(url,{headers:hdrs()});const t=await r.text();let d;try{d=JSON.parse(t);}catch{d={ok:false,error:t}};if(!r.ok)throw d;return d;}
async function jpost(url,body){const r=await fetch(url,{method:'POST',headers:hdrs(),body:JSON.stringify(body||{})});const t=await r.text();let d;try{d=JSON.parse(t);}catch{d={ok:false,error:t}};if(!r.ok)throw d;return d;}
function setConn(ok,msg){$('conn').textContent=msg;$('conn').className='pill '+(ok?'ok':'bad');$('last').textContent='Last: '+new Date().toLocaleTimeString();}
function renderSessions(items){items=(items||[]).slice().sort((a,b)=>(b.updatedAt||'').localeCompare(a.updatedAt||''));
 const rows=items.slice(0,20).map(s=>`<tr><td><code>${s.key||''}</code></td><td>${s.sessionId||''}</td><td>${s.updatedAt||''}</td><td>${age(s.updatedAt)}</td></tr>`).join('');
 $('sessionsTbl').innerHTML=`<table class="table"><thead><tr><th>key</th><th>sessionId</th><th>updatedAt</th><th>age</th></tr></thead><tbody>${rows||'<tr><td colspan="4" class="muted">No sessions</td></tr>'}</tbody></table>`;
 $('kSessions').textContent=String(items.length);$('kSessionFresh').textContent=items[0]?('latest '+age(items[0].updatedAt)):'no activity';}
function cronHealth(jobs){const total=jobs.length,en=jobs.filter(j=>j.enabled).length,err=jobs.filter(j=>(j.consecutiveErrors||0)>0).length;return `${en}/${total} enabled, ${err} with errors`;}
function renderCron(jobs){jobs=jobs||[];$('kCron').textContent=String(jobs.length);$('kCronHealth').textContent=cronHealth(jobs);
 const box=$('cronQuick'); box.innerHTML='';
 jobs.slice(0,12).forEach(j=>{const d=document.createElement('div');d.className='row';d.innerHTML=`<span class="pill ${j.enabled?'ok':'warn'}">${j.enabled?'on':'off'}</span><b>${j.name||j.id}</b><span class="muted">next: ${j.nextRunAt||'-'}</span><span class="muted">last: ${j.lastRunAt||'-'}</span><span class="muted">err: ${j.consecutiveErrors||0}</span><button data-id="${j.id}" data-a="run">Run due</button><button data-id="${j.id}" data-a="enable">Enable</button><button data-id="${j.id}" data-a="disable">Disable</button>`;box.appendChild(d);});
 box.querySelectorAll('button').forEach(b=>b.onclick=async()=>{const id=b.dataset.id,a=b.dataset.a;try{if(a==='run') await jpost('/api/cron/jobs/'+id+'/run',{mode:'due'}); else await jpost('/api/cron/jobs/'+id+'/'+a,{}); await refreshAll();}catch(e){alert((e&&e.error)||fmt(e));}});
 $('cronRaw').textContent=fmt(jobs.slice(0,20));}
function applyOverview(o){$('overview').textContent=fmt(o);$('kService').textContent=o.service||'nexaclaw';$('kNow').textContent=o.now||'';$('kUptime').textContent=msTo(o.uptimeMs||0);$('kAuth').textContent='auth: '+(o.authMode||'off');}
async function refreshAll(){ $('tok').value=getTok(); try{const [ov,ss,cr,lg,au]=await Promise.all([jget('/api/admin/overview'),jget('/api/sessions'),jget('/api/cron/jobs'),jget('/api/admin/logs/tail?limit=40'),jget('/api/admin/audit/tail?limit=40')]); applyOverview(ov); renderSessions(ss.sessions||[]); renderCron(cr.jobs||[]); $('logs').textContent=fmt(lg.items||lg); $('audit').textContent=fmt(au.items||au); setConn(true,'ok'); }catch(e){$('overview').textContent=fmt({ok:false,error:e&&e.error?e.error:fmt(e)}); setConn(false,'error');}}
function resetTimer(){ if(timer){clearInterval(timer);timer=null;} const ms=Number($('refreshMs').value||0); if(ms>0&&!paused){timer=setInterval(refreshAll,ms);} }
$('saveTok').onclick=()=>{localStorage.setItem(LS_TOKEN,$('tok').value||'');};
$('refreshBtn').onclick=refreshAll;
$('refreshMs').onchange=()=>{localStorage.setItem(LS_REFRESH,$('refreshMs').value);resetTimer();};
$('toggleAuto').onclick=()=>{paused=!paused;$('toggleAuto').textContent=paused?'Resume':'Pause';resetTimer();};
(function init(){ $('tok').value=getTok(); const saved=localStorage.getItem(LS_REFRESH)||'10000'; $('refreshMs').value=saved; resetTimer(); refreshAll(); })();
</script></body></html>)HTML";
    res.set_content(kAdminHtml, "text/html; charset=utf-8");
  });

  server_.Get("/api/status", [&](const httplib::Request&, httplib::Response& res) {
    replyJson(res, {{"ok", true},
                    {"service", "nexaclaw"},
                    {"now", util::TimeUtil::nowIso8601()},
                    {"uptimeMs", util::TimeUtil::nowMillis() - startedAtMs_},
                    {"sessions", {{"count", sessions_.listSessions().size()}}},
                    {"jobs", {{"count", cron_.listJobs().size()}}},
                    {"tools", {{"count", tools_.list().size()}, {"allowed", tools_.allowedTools()}}}});
  });

  server_.Get("/api/admin/overview", [&](const httplib::Request&, httplib::Response& res) {
    auto sessions = sessions_.listSessions();
    std::sort(sessions.begin(), sessions.end(), [](const auto& a, const auto& b) { return a.updatedAt > b.updatedAt; });

    const auto jobs = cron_.listJobs();
    int enabledJobs = 0;
    int jobsWithErrors = 0;
    for (const auto& j : jobs) {
      if (j.enabled) ++enabledJobs;
      if (j.consecutiveErrors > 0) ++jobsWithErrors;
    }

    json recentSessions = json::array();
    for (std::size_t i = 0; i < std::min<std::size_t>(sessions.size(), 10); ++i) {
      recentSessions.push_back({{"key", sessions[i].key}, {"sessionId", sessions[i].sessionId}, {"updatedAt", sessions[i].updatedAt}});
    }

    json cronSample = json::array();
    for (std::size_t i = 0; i < std::min<std::size_t>(jobs.size(), 10); ++i) {
      const auto& j = jobs[i];
      cronSample.push_back({{"id", j.id},
                            {"name", j.name},
                            {"kind", j.kind},
                            {"enabled", j.enabled},
                            {"nextRunAt", j.nextRunAt},
                            {"lastRunAt", j.lastRunAt},
                            {"lastSuccessAt", j.lastSuccessAt},
                            {"consecutiveErrors", j.consecutiveErrors}});
    }

    const auto cronStatus = cron_.status();
    const auto browserStatus = browser_.status();
    replyJson(res, {{"ok", true},
                    {"service", "nexaclaw"},
                    {"now", util::TimeUtil::nowIso8601()},
                    {"uptimeMs", util::TimeUtil::nowMillis() - startedAtMs_},
                    {"authMode", authConfig_.mode},
                    {"sessions", {{"count", sessions.size()}, {"recent", recentSessions}}},
                    {"tasks", {{"summary", tasks_.list().value("summary", json::object())}}},
                    {"cron", {{"status", cronStatus},
                              {"count", jobs.size()},
                              {"enabled", enabledJobs},
                              {"jobsWithErrors", jobsWithErrors},
                              {"sample", cronSample}}},
                    {"browser", browserStatus},
                    {"recentEvents", readEventTail(10)}});
  });

  server_.Get("/api/admin/audit/tail", [&](const httplib::Request& req, httplib::Response& res) {
    const int limit = parsePositiveLimit(req, "limit", 30, 200);
    replyJson(res, {{"ok", true}, {"limit", limit}, {"path", auditFilePath_.string()}, {"items", readAuditTail(limit)}});
  });

  server_.Get("/api/admin/logs/tail", [&](const httplib::Request& req, httplib::Response& res) {
    const int limit = parsePositiveLimit(req, "limit", 30, 200);
    replyJson(res, {{"ok", true}, {"limit", limit}, {"items", readEventTail(limit)}});
  });

  server_.Get("/api/events/stream", [&](const httplib::Request&, httplib::Response& res) {
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");

    auto subscriber = events_.subscribe();
    res.set_chunked_content_provider(
        "text/event-stream",
        [subscriber = std::move(subscriber)](size_t /*offset*/, httplib::DataSink& sink) mutable {
          auto event = subscriber.waitNext(1000);
          if (!event.has_value()) {
            const std::string keepalive = ": keepalive\n\n";
            sink.write(keepalive.data(), keepalive.size());
            return true;
          }

          json payload = {
              {"id", event->id},
              {"type", event->type},
              {"ts", event->ts},
              {"data", event->data},
          };
          const std::string frame = "id: " + std::to_string(event->id) + "\n" +
                                    "event: " + event->type + "\n" +
                                    "data: " + payload.dump() + "\n\n";
          sink.write(frame.data(), frame.size());
          return true;
        });
  });

  server_.Get("/api/browser/status", [&](const httplib::Request&, httplib::Response& res) {
    replyJson(res, browser_.status());
  });

  server_.Post("/api/browser/open", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    const auto result = browser_.open(body.value("url", ""));
    audit_.write("browser_open", {{"ok", result.value("ok", false)}, {"url", body.value("url", "")}});
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Post("/api/browser/navigate", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    const auto result = browser_.navigate(body.value("url", ""), body.value("targetId", ""));
    audit_.write("browser_navigate", {{"ok", result.value("ok", false)}, {"url", body.value("url", "")}, {"targetId", body.value("targetId", "")}});
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Post("/api/browser/snapshot", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    const auto result = browser_.snapshot(body.value("url", ""), body.value("targetId", ""));
    audit_.write("browser_snapshot", {{"ok", result.value("ok", false)}, {"url", body.value("url", "")}, {"targetId", body.value("targetId", "")}});
    replyJson(res, result, result.value("ok", false) ? 200 : 501);
  });

  server_.Post("/api/browser/click", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    const auto result = browser_.click(body.value("ref", ""), body.value("targetId", ""), body.value("double", false));
    audit_.write("browser_click", {{"ok", result.value("ok", false)}, {"ref", body.value("ref", "")}});
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Post("/api/browser/type", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    const auto result = browser_.type(body.value("ref", ""), body.value("text", ""), body.value("targetId", ""),
                                      body.value("submit", false), body.value("slowly", false));
    audit_.write("browser_type", {{"ok", result.value("ok", false)}, {"ref", body.value("ref", "")}});
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Post("/api/browser/screenshot", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    const auto result = browser_.screenshot(body.value("targetId", ""), body.value("fullPage", false), body.value("type", "png"));
    audit_.write("browser_screenshot", {{"ok", result.value("ok", false)}});
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Post("/api/message", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);

    const std::string sessionKey = deriveApiSessionKey(body);
    const std::string text = body.value("text", "");
    const bool systemEvent = body.value("systemEvent", false);
    if (text.empty()) return replyJson(res, {{"ok", false}, {"error", "text is required"}}, 400);

    try {
      const std::string reply = agent_.handleMessage(sessionKey, text, systemEvent);
      audit_.write("message", {{"channel", "api"}, {"sessionKey", sessionKey}});
      replyJson(res, {{"ok", true}, {"sessionKey", sessionKey}, {"reply", reply}});
    } catch (const std::exception& e) {
      replyJson(res, {{"ok", false}, {"error", e.what()}}, 500);
    }
  });

  server_.Post("/api/inbound", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);

    const std::string channel = body.value("channel", "");
    const std::string peerId = body.value("peerId", "");
    const std::string text = body.value("text", "");
    const bool systemEvent = body.value("systemEvent", false);
    if (channel.empty() || text.empty()) return replyJson(res, {{"ok", false}, {"error", "channel and text are required"}}, 400);

    try {
      const std::string sessionKey = agent_.deriveSessionKey(channel, peerId);
      const std::string reply = agent_.routeInboundMessage(channel, peerId, text, systemEvent);
      audit_.write("inbound", {{"channel", channel}, {"peerId", peerId}, {"sessionKey", sessionKey}});
      replyJson(res, {{"ok", true}, {"channel", channel}, {"peerId", peerId}, {"sessionKey", sessionKey}, {"reply", reply}});
    } catch (const std::exception& e) {
      replyJson(res, {{"ok", false}, {"error", e.what()}}, 500);
    }
  });

  server_.Get("/api/tasks", [&](const httplib::Request&, httplib::Response& res) { replyJson(res, tasks_.list()); });
  server_.Post("/api/tasks", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    auto result = tasks_.enqueue(body);
    audit_.write("task_enqueue", {{"ok", result.value("ok", false)}});
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });
  server_.Get(R"(/api/tasks/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "task id missing"}}, 400);
    auto result = tasks_.get(req.matches[1].str());
    replyJson(res, result, result.value("ok", false) ? 200 : 404);
  });
  server_.Post(R"(/api/tasks/(.+)/cancel)", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "task id missing"}}, 400);
    auto result = tasks_.cancel(req.matches[1].str());
    audit_.write("task_cancel", {{"id", req.matches[1].str()}, {"ok", result.value("ok", false)}});
    replyJson(res, result, result.value("ok", false) ? 200 : 404);
  });

  server_.Get("/api/sessions", [&](const httplib::Request&, httplib::Response& res) {
    json arr = json::array();
    for (const auto& s : sessions_.listSessions()) arr.push_back({{"key", s.key}, {"sessionId", s.sessionId}, {"updatedAt", s.updatedAt}});
    replyJson(res, {{"ok", true}, {"sessions", arr}});
  });

  server_.Get("/api/tools", [&](const httplib::Request&, httplib::Response& res) {
    replyJson(res, {{"ok", true}, {"tools", tools_.list()}, {"allowedTools", tools_.allowedTools()}});
  });

  server_.Post(R"(/api/tools/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "Tool name missing"}}, 400);
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);

    tools::ToolCallContext ctx;
    ctx.source = "api-tools-endpoint";
    ctx.channel = body.value("channel", "api");
    ctx.peerId = body.value("peerId", "");

    auto result = tools_.call(req.matches[1].str(), body, ctx);
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Get("/api/cron/status", [&](const httplib::Request&, httplib::Response& res) {
    replyJson(res, cron_.status());
  });

  server_.Get("/api/cron/jobs", [&](const httplib::Request&, httplib::Response& res) {
    json arr = json::array();
    for (const auto& j : cron_.listJobs()) {
      arr.push_back({{"id", j.id},
                     {"name", j.name},
                     {"description", j.description},
                     {"kind", j.kind},
                     {"everyMs", j.everyMs},
                     {"at", j.atIso},
                     {"cron", j.cronExpr},
                     {"schedule", {{"kind", j.kind}, {"everyMs", j.everyMs}, {"at", j.atIso}, {"expr", j.cronExpr}, {"tz", j.tz}}},
                     {"nextRunAt", j.nextRunAt},
                     {"sessionKey", j.sessionKey},
                     {"message", j.message},
                     {"sessionTarget", j.sessionTarget},
                     {"wakeMode", j.wakeMode},
                     {"agentId", j.agentId},
                     {"deleteAfterRun", j.deleteAfterRun},
                     {"payload", {{"kind", j.payload.kind}, {"text", j.payload.text}, {"message", j.payload.text}, {"model", j.payload.model}, {"thinking", j.payload.thinking}, {"timeoutSeconds", j.payload.timeoutSeconds}}},
                     {"delivery", {{"mode", j.delivery.mode}, {"channel", j.delivery.channel}, {"to", j.delivery.to}, {"bestEffort", j.delivery.bestEffort}}},
                     {"enabled", j.enabled},
                     {"consecutiveErrors", j.consecutiveErrors},
                     {"lastRunAt", j.lastRunAt},
                     {"lastSuccessAt", j.lastSuccessAt}});
    }
    replyJson(res, {{"ok", true}, {"jobs", arr}});
  });

  server_.Post("/api/cron/jobs", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    auto result = cron_.addJob(body);
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Patch(R"(/api/cron/jobs/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "job id missing"}}, 400);
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    auto result = cron_.updateJob(req.matches[1].str(), body);
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Post(R"(/api/cron/jobs/(.+)/enable)", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "job id missing"}}, 400);
    auto result = cron_.setEnabled(req.matches[1].str(), true);
    replyJson(res, result, result.value("ok", false) ? 200 : 404);
  });

  server_.Post(R"(/api/cron/jobs/(.+)/disable)", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "job id missing"}}, 400);
    auto result = cron_.setEnabled(req.matches[1].str(), false);
    replyJson(res, result, result.value("ok", false) ? 200 : 404);
  });

  server_.Post(R"(/api/cron/jobs/(.+)/run)", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "job id missing"}}, 400);
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    const std::string mode = body.value("mode", "force");
    auto result = cron_.runNow(req.matches[1].str(), mode);
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Post(R"(/api/cron/jobs/(.+)/run-now)", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "job id missing"}}, 400);
    auto result = cron_.runNow(req.matches[1].str(), "force");
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Get(R"(/api/cron/jobs/(.+)/runs)", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "job id missing"}}, 400);
    int limit = 20;
    if (req.has_param("limit")) {
      try {
        limit = std::max(1, std::stoi(req.get_param_value("limit")));
      } catch (...) {
        limit = 20;
      }
    }
    auto result = cron_.listRuns(req.matches[1].str(), limit);
    replyJson(res, result, result.value("ok", false) ? 200 : 404);
  });

  server_.Post("/api/cron/validate", [&](const httplib::Request& req, httplib::Response& res) {
    const auto body = parseBody(req);
    if (body.is_discarded()) return replyJson(res, {{"ok", false}, {"error", "Invalid JSON"}}, 400);
    auto result = cron_.validate(body);
    replyJson(res, result, result.value("ok", false) ? 200 : 400);
  });

  server_.Delete(R"(/api/cron/jobs/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) return replyJson(res, {{"ok", false}, {"error", "job id missing"}}, 400);
    const bool removed = cron_.removeJob(req.matches[1].str());
    replyJson(res, {{"ok", removed}, {"id", req.matches[1].str()}, {"error", removed ? "" : "job not found"}}, removed ? 200 : 404);
  });
}

bool HttpServer::start() {
  core::Logger::info("HTTP server listening on " + host_ + ":" + std::to_string(port_));
  return server_.listen(host_.c_str(), port_);
}

void HttpServer::stop() { server_.stop(); }

}  // namespace clawforge::http
