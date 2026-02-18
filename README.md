# ClawForge

**Practical self-hosted AI gateway in C++** (OpenClaw-style architecture, lightweight core).

ClawForge is built as a local-first control plane: sessions, tools, cron, Telegram baseline, realtime events, task lane, security guards, and ops scripts.

- 🇷🇺 Russian README: [README.ru.md](./README.ru.md)
- Parity matrix: [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md)
- Stage docs: [STAGE4](./docs/STAGE4.md) · [STAGE5](./docs/STAGE5.md) · [STAGE6](./docs/STAGE6.md) · [STAGE7](./docs/STAGE7.md) · [STAGE8](./docs/STAGE8.md)

---

## What is already working

- HTTP API gateway: `/health`, `/api/status`, `/api/message`, `/api/inbound`, `/api/tools`, `/api/sessions`
- Realtime SSE stream: `/api/events/stream`
- Cron engine: `every` / `at` / `cron` + validate + run-now
- Session store + JSONL transcripts
- Scoped tools policy (`global` / `channels` / `peers`)
- Telegram baseline + pairing policy/approvals
- Task lane API (`/api/tasks`) with timeout/cancel baseline
- Security baseline: auth token mode, per-source rate limiting, audit JSONL
- Browser relay baseline endpoints (`status/open`) + honest snapshot diagnostic stub
- RU/EN CLI UX + doctor + smoke/benchmark scripts

---

## Quick start (5 minutes)

### Requirements

- macOS/Linux
- C++20 compiler (AppleClang/clang/g++)
- CMake >= 3.20
- `curl`, `python3`, `bash`

### Run

```bash
cd ~/PycharmProjects/ClawForgeProject
cp config/config.example.json config/config.json

# Required for LLM responses
export OPENAI_API_KEY="<your_key>"

scripts/bootstrap.sh
./build/clawforge run --config config/config.json
```

Health check:

```bash
curl -s http://127.0.0.1:18890/health
```

---

## CLI cheat sheet

```bash
./build/clawforge --help
./build/clawforge --doctor --config config/config.json
./build/clawforge status --config config/config.json
./build/clawforge cron list --config config/config.json
./build/clawforge tools list --config config/config.json
./build/clawforge pairing list --config config/config.json
./build/clawforge pairing approve <code> --config config/config.json
```

Language:

```bash
./build/clawforge --lang ru --help
./build/clawforge --lang en --help
```

---

## API quick examples

### Optional auth (token mode)

If `gateway.auth.mode = "token"`, send:

```bash
-H "Authorization: Bearer $CLAWFORGE_GATEWAY_TOKEN"
```

### Core

```bash
curl -s http://127.0.0.1:18890/api/status
curl -s -X POST http://127.0.0.1:18890/api/message \
  -H 'Content-Type: application/json' \
  -d '{"sessionKey":"main","text":"Hello"}'
```

### Inbound router

```bash
curl -s -X POST http://127.0.0.1:18890/api/inbound \
  -H 'Content-Type: application/json' \
  -d '{"channel":"telegram","peerId":"123","text":"/status"}'
```

### Realtime events (SSE)

```bash
curl -N http://127.0.0.1:18890/api/events/stream
```

### Browser baseline

```bash
curl -s http://127.0.0.1:18890/api/browser/status
curl -s -X POST http://127.0.0.1:18890/api/browser/open \
  -H 'Content-Type: application/json' -d '{"url":"https://example.com"}'
curl -s -X POST http://127.0.0.1:18890/api/browser/snapshot \
  -H 'Content-Type: application/json' -d '{"url":"https://example.com"}'
```

### Tasks

```bash
curl -s -X POST http://127.0.0.1:18890/api/tasks \
  -H 'Content-Type: application/json' \
  -d '{"channel":"api","peerId":"demo","text":"/status","timeoutMs":5000}'
curl -s http://127.0.0.1:18890/api/tasks
```

---

## Quality checks

```bash
# full smoke pipeline
scripts/smoke_full.sh

# stage-focused
scripts/smoke_stage5.sh
scripts/smoke_stage6.sh

# strict compile gates
cmake -S . -B build_warnings -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build_warnings -j

# quick perf sanity
scripts/benchmark_quick.sh 50
```

---

## Deploy templates

- systemd: `deploy/clawforge.service`
- launchd: `deploy/com.clawforge.agent.plist`

---

## Configuration highlights

See `config/config.example.json`.

Key blocks:

- `gateway.auth` — off/token auth mode
- `gateway.messageQueueTimeoutMs`
- `telegram.dmPolicy` — `open|allowlist|pairing|disabled`
- `api.dmScope` — `main|per-peer|per-channel-peer`
- `toolsPolicy.scopes` — global/channel/peer policies
- `browser` — backend and diagnostics
- `taskLane` — queue + timeout
- `rateLimit` — per-source request limits
- `audit` — JSONL audit trail path

---

## Known limits (honest)

- Browser snapshot is still diagnostic stub (no real DOM/image capture yet)
- Telegram is baseline, not full multi-channel ecosystem
- No Canvas/Nodes integration yet
- No full external subagent runtime orchestration

For up-to-date details, check [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md).
