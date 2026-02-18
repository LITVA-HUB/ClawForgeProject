# PARITY ROADMAP (ClawForge vs OpenClaw)

> Updated after Stage 9.

## Practical parity matrix

| Area | OpenClaw | ClawForge (Stage 9) | Status | Notes |
|---|---|---|---|---|
| HTTP API `/health`, `/api/message`, `/api/status` | ✅ | ✅ | Closed | Stable local gateway |
| Session store | ✅ | ✅ | Closed | File-backed sessions |
| Tools registry + scoped policy | ✅ | ✅ | Closed | global/channel/peer policy |
| Cron jobs (`every`, `at`, `cron`) | ✅ | ⚠️ | Partial | API complete baseline, CLI `cron list` |
| CLI UX + doctor | ✅ | ✅ | Closed | RU/EN + compatibility dispatcher |
| Multi-model providers + aliases + fallbacks | ✅ | ✅ | Closed (baseline) | openai/anthropic/openrouter/gemini/minimax |
| OpenClaw CLI compatibility layer | ✅ | ⚠️ | Partial | implemented core + stubs for not-yet branches |
| Telegram channel baseline | ✅ | ⚠️ | Partial | polling+pairing, no webhooks/sharding |
| Browser relay endpoints | ✅ | ⚠️ | Partial | status/open real, snapshot diagnostic stub |
| Realtime events | ✅ | ✅ | Closed | SSE + EventBus |
| Async task/job lane | ✅ | ⚠️ | Partial | internal queue, single worker |
| Rate limiting + audit trail + recovery | ✅ | ✅ | Closed (baseline) | source RL, JSONL audit, task restore |
| Canvas/Nodes/device control | ✅ | ❌ | Open | not implemented |
| Multi-channel plugins (Discord/Slack/Signal/etc) | ✅ | ❌ | Open | Telegram-only baseline |
| Advanced subagent orchestration | ✅ | ❌ | Open | no external runtime orchestration |

## Next recommended step

Expand compatibility stubs branch-by-branch (`gateway`, `browser`, `system`, `channels`) into real implementations and add CLI e2e tests for parity-critical flows.
