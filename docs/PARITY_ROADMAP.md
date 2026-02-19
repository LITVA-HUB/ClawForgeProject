# PARITY ROADMAP (NexaClaw vs OpenClaw)

> Updated after Stage 18 slice 1 (native browser backend baseline + OpenClaw CLI fallback).

## Practical parity matrix

| Area | OpenClaw | NexaClaw (Stage 18-s1) | Status | Notes |
|---|---|---|---|---|
| HTTP API `/health`, `/api/message`, `/api/status` | ✅ | ✅ | Closed | Stable local gateway |
| Session store | ✅ | ✅ | Closed | File-backed sessions |
| Tools registry + scoped policy | ✅ | ✅ | Closed | global/channel/peer policy |
| Cron jobs (`every`, `at`, `cron`) | ✅ | ⚠️ | Partial (Stage 14 baseline) | `status/edit/enable/disable/runs`, sessionTarget/payload contracts, run history + retry backoff |
| CLI UX + doctor | ✅ | ✅ | Closed | RU/EN + compatibility dispatcher |
| Setup/onboard/configure wizard | ✅ | ⚠️ | Partial (Stage 13 baseline) | bilingual terminal setup wizard + non-interactive mode |
| Gateway control-plane (`gateway status/start/stop/restart/call`) | ✅ | ⚠️ | Partial (Stage 12 baseline) | local pid/log process management + config.get/apply/patch baseline |
| Security audit (`security audit [--deep] [--fix]`) | ✅ | ⚠️ | Partial (Stage 12 baseline) | dmScope/auth/perms checks + safe fixes |
| Message command/action surface | ✅ | ⚠️ | Partial (Stage 16 baseline) | telegram `send/react/delete/poll` + strict target validation + dry-run |
| Channels command family | ✅ | ⚠️ | Partial (Stage 15 baseline) | `channels list/status/capabilities/resolve/add/remove` (telegram baseline) |
| Multi-model providers + aliases + fallbacks | ✅ | ✅ | Closed (baseline) | openai/anthropic/openrouter/gemini/minimax |
| Models auth parity beyond API keys | ✅ | ⚠️ | Partial (Stage 17 slice 2) | Native device-code start/login/poll baseline for `openai-codex` + legacy OpenClaw import bridge fallback |
| OpenClaw CLI compatibility layer | ✅ | ⚠️ | Partial | implemented core + explicit stubs for not-yet branches |
| Telegram channel baseline | ✅ | ⚠️ | Partial | polling+pairing, no webhooks/sharding |
| Browser relay endpoints | ✅ | ⚠️ | Partial (Stage 18 slice 1 baseline) | Native backend baseline (`browser.backend=native`) for core flow + `openclaw_cli` fallback for real OpenClaw automation |
| Realtime events | ✅ | ✅ | Closed | SSE + EventBus |
| Async task/job lane | ✅ | ⚠️ | Partial | internal queue, single worker |
| Rate limiting + audit trail + recovery | ✅ | ✅ | Closed (baseline) | source RL, JSONL audit, task restore |
| Canvas/Nodes/device control | ✅ | ❌ | Open | not implemented |
| Multi-channel plugins (Discord/Slack/Signal/etc) | ✅ | ❌ | Open | Telegram-first baseline |
| Advanced subagent orchestration | ✅ | ❌ | Open | no external runtime orchestration |

## Next recommended step

After Stage 18 slice 1, the heaviest remaining parity items are:

1. Upgrade native browser backend from diagnostic baseline to full Playwright/CDP control
2. Channels multi-provider expansion (Discord/Slack/Signal/etc) beyond Telegram baseline
3. `agents` / `agent` command-family baseline (list/add/delete/targeted runs)
4. Multi-channel/plugin + nodes/canvas ecosystem parity
