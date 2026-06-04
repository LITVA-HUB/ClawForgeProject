# NexaClaw HTTP API Reference

Base URL: `http://127.0.0.1:18890` (configurable via `http.host` / `http.port`)

If `gateway.auth.mode = "token"`, all endpoints require:
```
Authorization: Bearer $NEXACLAW_GATEWAY_TOKEN
```

---

## System

### GET /health

Returns gateway liveness.

**Response 200:**
```json
{"status": "ok"}
```

### GET /api/status

Returns runtime summary.

**Response 200:**
```json
{
  "status": "ok",
  "gateway": "running",
  "sessions": 1,
  "cronJobs": 3
}
```

---

## Messaging

### POST /api/message

Send a message to a session.

**Body:**
```json
{
  "sessionKey": "main",
  "text": "Hello"
}
```

**Response 200:**
```json
{"ok": true, "reply": "..."}
```

### POST /api/inbound

Route an inbound message from a channel.

**Body:**
```json
{
  "channel": "telegram",
  "peerId": "123456",
  "text": "/status"
}
```

---

## Sessions

### GET /api/sessions

List active sessions.

### DELETE /api/sessions/:key

Clear a session transcript.

---

## Tools

### GET /api/tools

List registered tools with their policy scope.

### POST /api/tools/:name

Call a tool by name.

**Body:** tool-specific JSON payload.

---

## Tasks

### POST /api/tasks

Enqueue an async task.

**Body:**
```json
{
  "channel": "api",
  "peerId": "demo",
  "text": "/status",
  "timeoutMs": 5000
}
```

**Response 202:**
```json
{"taskId": "abc123", "status": "queued"}
```

### GET /api/tasks

List all tasks with their status.

### GET /api/tasks/:id

Get a single task.

### DELETE /api/tasks/:id

Cancel a running task.

### GET /api/tasks/:id/events

Stream run events for a task (SSE or JSON array).

---

## Events

### GET /api/events/stream

Server-Sent Events stream of gateway events.

**Event format:**
```
data: {"type":"message","sessionKey":"main","text":"Hello","ts":1717000000}
```

---

## Browser

### GET /api/browser/status

Report browser backend status and capability gates.

### POST /api/browser/open

```json
{"url": "https://example.com"}
```

### POST /api/browser/navigate

```json
{"url": "https://example.com"}
```

### POST /api/browser/snapshot

```json
{"url": "https://example.com"}
```

Returns page snapshot (HTML/text).

### POST /api/browser/click

```json
{"selector": "e6"}
```

### POST /api/browser/type

```json
{"selector": "input[name=q]", "text": "nexaclaw"}
```

### POST /api/browser/screenshot

Returns base64-encoded screenshot.

---

## Admin

### GET /admin

Operator dashboard (HTML). Shows KPI cards, session/cron state, event log, audit tail.

---

## Error format

All error responses use:

```json
{"error": "error_code", "message": "Human-readable description"}
```

Common HTTP status codes:
- `400` — bad request / missing fields
- `401` — unauthorized (auth token mode)
- `404` — resource not found
- `429` — rate limit exceeded
- `500` — internal gateway error
