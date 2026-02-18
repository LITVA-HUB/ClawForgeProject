# PARITY ROADMAP (ClawForge vs OpenClaw)

> Updated after Stage 8.

## Practical parity matrix

| Area | OpenClaw | ClawForge (Stage 8) | Status | Notes |
|---|---|---|---|---|
| HTTP API `/health`, `/api/message`, `/api/status` | ✅ | ✅ | Closed | Stable local gateway |
| Session store | ✅ | ✅ | Closed | File-backed sessions |
| Tools registry + scoped policy | ✅ | ✅ | Closed | global/channel/peer policy |
| Cron jobs (`every`, `at`, `cron`) | ✅ | ✅ | Closed | validate + run-now + API |
| CLI UX + doctor | ✅ | ✅ | Closed | RU/EN, doctor expanded |
| Telegram channel baseline | ✅ | ⚠️ | Partial | polling+pairing, no webhooks/sharding |
| Browser relay endpoints | ✅ | ⚠️ | Partial | status/open real, snapshot diagnostic stub |
| Realtime events | ✅ | ✅ | Closed | SSE + EventBus |
| Async task/job lane | ✅ | ⚠️ | Partial | internal queue, single worker |
| Rate limiting + audit trail + recovery | ✅ | ✅ | Closed (baseline) | source RL, JSONL audit, task restore |
| Canvas/Nodes/device control | ✅ | ❌ | Open | not implemented |
| Multi-channel plugins (Discord/Slack/Signal/etc) | ✅ | ❌ | Open | Telegram-only baseline |
| Advanced subagent orchestration | ✅ | ❌ | Open | no external runtime orchestration |

## Why some items remain open

They require heavy external dependencies and runtime integration (browser automation framework, node/device bridges, plugin ecosystem), which are intentionally not bundled into the current minimal C++ core.

## Recommended next big step

Implement a real browser backend (Playwright/CDP) and task-integrated browser sessions; this unlocks the largest user-visible parity jump while reusing current API/task/audit foundations.
