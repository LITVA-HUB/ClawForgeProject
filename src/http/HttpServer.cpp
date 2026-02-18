#include "http/HttpServer.hpp"

#include <algorithm>
#include <cstdlib>

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

  server_.Get("/api/status", [&](const httplib::Request&, httplib::Response& res) {
    replyJson(res, {{"ok", true},
                    {"service", "nexaclaw"},
                    {"now", util::TimeUtil::nowIso8601()},
                    {"uptimeMs", util::TimeUtil::nowMillis() - startedAtMs_},
                    {"sessions", {{"count", sessions_.listSessions().size()}}},
                    {"jobs", {{"count", cron_.listJobs().size()}}},
                    {"tools", {{"count", tools_.list().size()}, {"allowed", tools_.allowedTools()}}}});
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
