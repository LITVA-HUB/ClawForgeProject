# NexaClaw

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![CMake](https://img.shields.io/badge/build-CMake%203.20%2B-informational)
![Platforms](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey)

**NexaClaw is a practical self-hosted AI gateway and local-first control plane for developers building AI agents.**

It provides sessions, tools, cron automation, realtime events, task lanes, Telegram integration, browser relay primitives, model routing, security guards, CLI workflows, audit logs, and ops scripts in a lightweight C++20 codebase.

> **Repository note:** the project name is **NexaClaw**. The repository still uses the legacy name `ClawForgeProject` for migration continuity.

- 🇷🇺 Russian README: [README.ru.md](./README.ru.md)
- Parity matrix: [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md)
- CLI parity table: [docs/CLI_PARITY.md](./docs/CLI_PARITY.md)
- Stage docs: [STAGE4](./docs/STAGE4.md) · [STAGE5](./docs/STAGE5.md) · [STAGE6](./docs/STAGE6.md) · [STAGE7](./docs/STAGE7.md) · [STAGE8](./docs/STAGE8.md) · [STAGE10](./docs/STAGE10.md) · [STAGE11](./docs/STAGE11.md) · [STAGE12](./docs/STAGE12.md) · [STAGE13](./docs/STAGE13.md) · [STAGE14](./docs/STAGE14.md) · [STAGE15](./docs/STAGE15.md) · [STAGE16](./docs/STAGE16.md) · [STAGE18](./docs/STAGE18.md) · [STAGE19](./docs/STAGE19.md) · [STAGE28](./docs/STAGE28.md) · [STAGE29](./docs/STAGE29.md) · [STAGE30](./docs/STAGE30.md) · [STAGE31](./docs/STAGE31.md) · [STAGE32](./docs/STAGE32.md) · [STAGE33](./docs/STAGE33.md) · [STAGE34](./docs/STAGE34.md) · [STAGE12 gaps](./docs/STAGE12_CRITICAL_GAPS.md)

---

## Why this matters for OSS maintainers

Modern AI agents often depend on cloud APIs, brittle scripts, and opaque automation glue. NexaClaw focuses on the pieces maintainers can inspect, run locally, test, and harden:

- a local HTTP control plane for agent workflows;
- session and transcript storage with JSONL auditability;
- scoped tool policies for safer automation;
- cron and task-lane primitives for repeatable maintenance work;
- Telegram and browser-relay baselines for real operator workflows;
- CLI-first operations, smoke tests, and diagnostics.

The goal is to make agent infrastructure easier to self-host, debug, secure, and extend without turning every project into a large distributed system.

---

## Project status

NexaClaw is an active early-stage OSS project. Core control-plane flows are implemented and usable for local experiments, demos, and maintainer automation. Some compatibility and orchestration features are intentionally marked as partial until they are hardened.

Current maintainer focus:

- improve CI and release discipline;
- expand C++ test coverage and strict compile gates;
- harden token auth, rate limiting, tool scopes, and audit logging;
- improve CLI parity and migration docs;
- document safe self-hosting patterns for AI-agent gateways.

---

## What is already working

- HTTP API gateway: `/health`, `/api/status`, `/api/message`, `/api/inbound`, `/api/tools`, `/api/sessions`
- Realtime SSE stream: `/api/events/stream`
- Cron semantic baseline: `status/list/add/edit/enable/disable/run/runs/validate/rm` + sessionTarget/payload/delivery/wakeMode
- Gateway control-plane baseline: `gateway(run)|status|start|stop|restart|health|probe|call`
- Session store + JSONL transcripts
- Scoped tools policy (`global` / `channels` / `peers`)
- Telegram baseline + pairing policy/approvals + message actions (`send/react/delete/poll`) baseline
- Channels management baseline: `channels list|status|capabilities|resolve|add|remove` (telegram)
- Task lane API (`/api/tasks`) with timeout/cancel baseline + Stage34 run events introspection (`/api/tasks/{id}/events`) and richer transition state (`cancelling`)
- Security baseline: auth token mode, per-source rate limiting, audit JSONL
- Browser relay Stage 18 baseline: native backend (`browser.backend=native`) for `status/open/navigate/snapshot/click/type/screenshot`, with `openclaw_cli` fallback/compat mode
- Admin dashboard Stage 19 slice 2 operator console: `/admin` with KPI cards, refresh controls, session/cron visibility (state/next/last/errors), safe cron quick actions, event-log tail, and audit tail
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
git clone https://github.com/LITVA-HUB/ClawForgeProject.git
cd ClawForgeProject
cp config/config.example.json config/config.json

# Required for LLM responses
export OPENAI_API_KEY="<your_key>"

scripts/bootstrap.sh

# Optional but recommended: guided terminal setup wizard (RU/EN)
./build/nexaclaw setup --config config/config.json

./build/nexaclaw run --config config/config.json
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
- Script clones/updates repo, builds with CMake, installs `nexaclaw` binary.
- `--update` updates existing clone and re-installs binary.
- `--validate` checks prerequisites only and does not change the system.
- Prefer `--pin-commit` for deterministic install.
- `curl|bash` implies trust in repo owner and transport; inspect script when possible.

## CLI cheat sheet

```bash
./build/nexaclaw --help
./build/nexaclaw --doctor --config config/config.json
./build/nexaclaw status --config config/config.json
./build/nexaclaw cron list --config config/config.json
./build/nexaclaw tools list --config config/config.json
./build/nexaclaw pairing list --config config/config.json
./build/nexaclaw pairing approve <code> --config config/config.json
./build/nexaclaw models list --config config/config.json
./build/nexaclaw models status --config config/config.json
./build/nexaclaw models set anthropic/claude-3-5-haiku-latest --config config/config.json
./build/nexaclaw models aliases add fast anthropic/claude-3-5-haiku-latest --config config/config.json
./build/nexaclaw models fallbacks add openai/gpt-4o-mini --config config/config.json
./build/nexaclaw models auth add --provider openai --profile-id work --api-key-env OPENAI_API_KEY --config config/config.json
./build/nexaclaw models auth use --provider openai --profile-id work --config config/config.json
./build/nexaclaw models auth login --provider openai-codex --config config/config.json
./build/nexaclaw models auth order set --provider openai-codex --profile-id codex-work --profile-id codex-personal --config config/config.json
./build/nexaclaw models auth setup-token --provider openai-codex --profile-id codex --token <token> --expires-in 3600 --config config/config.json
./build/nexaclaw browser navigate https://example.com --config config/config.json
./build/nexaclaw browser click e6 --config config/config.json
./build/nexaclaw message react --channel telegram --target @chat --message-id 123 --emoji ✅ --dry-run --config config/config.json
./build/nexaclaw config get model.current --config config/config.json
./build/nexaclaw config set model.current fast --config config/config.json
```

## Multi-model routing

`modelsConfig` supports providers/models/routing:
- providers baseline: `openai`, `anthropic`, `openrouter`, `gemini`, `minimax`
- routing: `current` + `aliases` + `fallbacks`
- if provider API style is not supported, NexaClaw returns a diagnostic error with guidance.

## OpenClaw compatibility

NexaClaw Stage 10 provides practical CLI parity for high-value OpenClaw flows.

### 1:1 or near-1:1 mappings
- `status`, `health`, `doctor`, `sessions`
- `cron status|list|add|edit|enable|disable|run|runs|validate|rm`
- `tools list|call`
- `models list|status|set|aliases|fallbacks|probe|set-image`
- `models auth list|add|login|paste-token|setup-token|use|remove` + `models auth order get|set|clear`
- `image-fallbacks list|add|remove|clear`
- `browser status|open|navigate|snapshot|click|type|screenshot|act` (Stage 26/27/28 slices: OpenClaw-style `act` envelope baseline + native safe subset with constrained `evaluate` realism)
- `config get/set` (expanded key coverage)
- `logs tail`, `system event`, `pairing list|approve`
- `message send|react|delete|poll --channel telegram ...` (strict target validation + dry-run baseline)
- `channels list|status|capabilities|resolve|add|remove` (telegram baseline)

### Migration guide (OpenClaw -> NexaClaw)
- `openclaw browser status` -> `nexaclaw browser status`
- `openclaw browser open <url>` -> `nexaclaw browser open <url>`
- `openclaw cron add --json ...` -> `nexaclaw cron add --json ...`
- `openclaw tools call <name> --json ...` -> `nexaclaw tools call <name> --json ...`
- `openclaw models probe` -> `nexaclaw models probe`
- `openclaw models set-image <model>` -> `nexaclaw models set-image <model>`
- `openclaw image-fallbacks ...` -> `nexaclaw image-fallbacks ...`

For full command-by-command status (`implemented` / `partial` / `stub` / `impossible-now`), see `docs/CLI_PARITY.md`.

Language:

```bash
./build/nexaclaw --lang ru --help
./build/nexaclaw --lang en --help
```

---

## API quick examples

### Optional auth (token mode)

If `gateway.auth.mode = "token"`, send:

```bash
-H "Authorization: Bearer $NEXACLAW_GATEWAY_TOKEN"
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
scripts/smoke_stage12_gateway_security.sh
scripts/smoke_stage13_setup_wizard.sh
scripts/smoke_stage14_cron_semantics.sh
scripts/smoke_stage15_message_channels.sh
scripts/smoke_stage16_browser_oauth_message.sh
scripts/smoke_stage18_native_browser.sh
scripts/smoke_stage19_admin_dashboard.sh
scripts/smoke_stage21_agents.sh
scripts/smoke_stage22_nodes_canvas_devices.sh
scripts/smoke_stage29_agents_orchestration.sh
scripts/smoke_stage30_browser_wait_native.sh
scripts/smoke_stage31_agents_advanced.sh
scripts/smoke_stage32_agents_context_tools.sh
scripts/smoke_stage33_agents_policy_enforcement.sh
scripts/smoke_stage23_control_plane_slice1.sh
scripts/smoke_stage25_nodes_runtime_slice2.sh

# strict compile gates
cmake -S . -B build_warnings -DCMAKE_CXX_FLAGS='-Wall -Wextra -Wpedantic -Werror'
cmake --build build_warnings -j

# quick perf sanity
scripts/benchmark_quick.sh 50
```

---

## Deploy templates

- systemd: `deploy/nexaclaw.service`
- launchd: `deploy/com.nexaclaw.agent.plist`

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

- Browser parity now has a native NexaClaw baseline backend (`browser.backend=native`) for core command/API flow, including modeled GET form-submit side effects with structured capability gates; `browser.backend=openclaw_cli` remains available for real browser relay compatibility
- `agent/agents` CLI now has Stage33 runtime enforcement uplift: Stage32 metadata/forwarding plus live task-path enforcement for context carryover/history trimming and per-run tool allow/deny checks (`/api/tasks` + `/tool` command path); deep subagent orchestration parity is still partial
- Telegram is baseline, not full multi-channel ecosystem
- Canvas/Nodes/Devices have Stage 25 slice 2 practical runtime uplift: local read-safe runtime probe/invoke surface, virtual canvas snapshot, explicit capability-gated errors when runtime is unavailable
- No full external subagent runtime orchestration

For up-to-date details, check [docs/PARITY_ROADMAP.md](./docs/PARITY_ROADMAP.md).

---

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](./CONTRIBUTING.md) for development setup, quality checks, and pull request guidance.

Good first contributions:
- Improve documentation and examples.
- Add C++ tests for gateway behavior.
- Improve CLI help text and diagnostics.
- Add smoke tests for stable workflows.
- Improve CI and release automation.

---

## Security

NexaClaw is a local-first AI gateway. Security-sensitive areas include auth token mode, rate limiting, scoped tools, audit logs, browser relay behavior, Telegram pairing, and model-provider configuration.

Please do not open public GitHub issues for security vulnerabilities. See [SECURITY.md](./SECURITY.md) for the full policy and reporting instructions.

---

## License

[MIT](./LICENSE) — Copyright (c) 2026 LITVA-HUB
