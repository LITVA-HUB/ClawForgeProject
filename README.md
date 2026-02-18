# ClawForge

**Practical self-hosted AI gateway in C++** (OpenClaw-style architecture, lightweight core).

ClawForge is built as a local-first control plane: sessions, tools, cron, Telegram baseline, realtime events, task lane, security guards, and ops scripts.

- 🇷🇺 Russian README: [README.ru.md](./README.ru.md)
- Parity matrix: [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md)
- CLI parity table: [docs/CLI_PARITY.md](./docs/CLI_PARITY.md)
- Stage docs: [STAGE4](./docs/STAGE4.md) · [STAGE5](./docs/STAGE5.md) · [STAGE6](./docs/STAGE6.md) · [STAGE7](./docs/STAGE7.md) · [STAGE8](./docs/STAGE8.md) · [STAGE10](./docs/STAGE10.md) · [STAGE11](./docs/STAGE11.md)

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

## One-command install

User install (recommended):

```bash
curl -fsSL https://raw.githubusercontent.com/LITVA-HUB/ClawForgeProject/main/scripts/install.sh | bash
```

Safer variant (inspect first):

```bash
git clone https://github.com/LITVA-HUB/ClawForgeProject.git && cd ClawForgeProject && bash scripts/install.sh
```

System-wide install:

```bash
bash scripts/install.sh --system
# update existing installation
bash scripts/install.sh --update
```

Reproducible/pinned install:

```bash
bash scripts/install.sh --pin-commit <full_sha>
```

Dry-run (no changes):

```bash
bash scripts/install.sh --dry-run
# validate prerequisites only
bash scripts/install.sh --validate
```

Security notes:
- Script clones/updates repo, builds with CMake, installs `clawforge` binary.
- `--update` updates existing clone and re-installs binary.
- `--validate` checks prerequisites only and does not change the system.
- Prefer `--pin-commit` for deterministic install.
- `curl|bash` implies trust in repo owner and transport; inspect script when possible.

## CLI cheat sheet

```bash
./build/clawforge --help
./build/clawforge --doctor --config config/config.json
./build/clawforge status --config config/config.json
./build/clawforge cron list --config config/config.json
./build/clawforge tools list --config config/config.json
./build/clawforge pairing list --config config/config.json
./build/clawforge pairing approve <code> --config config/config.json
./build/clawforge models list --config config/config.json
./build/clawforge models status --config config/config.json
./build/clawforge models set anthropic/claude-3-5-haiku-latest --config config/config.json
./build/clawforge models aliases add fast anthropic/claude-3-5-haiku-latest --config config/config.json
./build/clawforge models fallbacks add openai/gpt-4o-mini --config config/config.json
./build/clawforge models auth add --provider openai --profile-id work --api-key-env OPENAI_API_KEY --config config/config.json
./build/clawforge models auth use --provider openai --profile-id work --config config/config.json
./build/clawforge models auth setup-token --provider openai-codex --profile-id codex --token <token> --expires-in 3600 --config config/config.json
./build/clawforge config get model.current --config config/config.json
./build/clawforge config set model.current fast --config config/config.json
```

## Multi-model routing

`modelsConfig` supports providers/models/routing:
- providers baseline: `openai`, `anthropic`, `openrouter`, `gemini`, `minimax`
- routing: `current` + `aliases` + `fallbacks`
- if provider API style is not supported, ClawForge returns a diagnostic error with guidance.

## OpenClaw compatibility

ClawForge Stage 10 provides practical CLI parity for high-value OpenClaw flows.

### 1:1 or near-1:1 mappings
- `status`, `health`, `doctor`, `sessions`
- `cron list|add|rm|run|validate`
- `tools list|call`
- `models list|status|set|aliases|fallbacks|probe|set-image`
- `models auth list|add|paste-token|setup-token|use|remove` (local auth profile store)
- `image-fallbacks list|add|remove|clear`
- `browser status|open|snapshot` (snapshot is still baseline/diagnostic)
- `config get/set` (expanded key coverage)
- `logs tail`, `system event`, `pairing list|approve`

### Migration guide (OpenClaw -> ClawForge)
- `openclaw browser status` -> `clawforge browser status`
- `openclaw browser open <url>` -> `clawforge browser open <url>`
- `openclaw cron add --json ...` -> `clawforge cron add --json ...`
- `openclaw tools call <name> --json ...` -> `clawforge tools call <name> --json ...`
- `openclaw models probe` -> `clawforge models probe`
- `openclaw models set-image <model>` -> `clawforge models set-image <model>`
- `openclaw image-fallbacks ...` -> `clawforge image-fallbacks ...`

For full command-by-command status (`implemented` / `partial` / `stub` / `impossible-now`), see `docs/CLI_PARITY.md`.

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
scripts/smoke_models_cli.sh
scripts/smoke_stage11_models_auth.sh
scripts/smoke_stage11_installer.sh

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
