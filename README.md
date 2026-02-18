# ClawForge (C++ OpenClaw-style assistant)

Practical self-hosted AI gateway in C++: HTTP API, sessions, tools, cron, Telegram baseline, browser relay baseline, async task lane, and ops tooling.

- RU docs: [`README.ru.md`](./README.ru.md)
- Stage docs: [`docs/STAGE6.md`](./docs/STAGE6.md), [`docs/STAGE7.md`](./docs/STAGE7.md), [`docs/STAGE8.md`](./docs/STAGE8.md)
- Parity matrix: [`docs/PARITY_ROADMAP.md`](./docs/PARITY_ROADMAP.md)

## Getting started

```bash
cd "пайчарм проджект"
scripts/bootstrap.sh
scripts/smoke_full.sh
./build/clawforge run --config config/config.json
```

## Main endpoints

- Core: `/health`, `/api/status`, `/api/message`, `/api/inbound`, `/api/sessions`, `/api/tools`
- Browser relay baseline: `/api/browser/status`, `/api/browser/open`, `/api/browser/snapshot`
- Cron: `/api/cron/*`
- Tasks (Stage 7): `/api/tasks`, `/api/tasks/<id>`, `/api/tasks/<id>/cancel`

## Ops scripts

- `scripts/bootstrap.sh`
- `scripts/smoke_stage6.sh`
- `scripts/smoke_full.sh`
- `scripts/benchmark_quick.sh [N]`

## Service templates

- systemd sample: `deploy/clawforge.service`
- launchd sample: `deploy/com.clawforge.agent.plist`

## Migration notes (Stage 5 -> 8)

- `toolsPolicy` now supports scoped schema (`scopes.global/channels/peers`).
- `security.requestsPerMinutePerSession` replaced by `rateLimit` block.
- New blocks: `taskLane`, `audit`, `browser`.
- Legacy top-level `toolsPolicy.allow/deny` is still supported.

## Known limits (honest)

- Browser snapshot is diagnostic stub; no real DOM/image capture yet.
- Async task lane is single-worker baseline.
- No Canvas/Nodes integration yet.
- No full plugin ecosystem (Telegram baseline only).
