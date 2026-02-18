# PARITY ROADMAP (NexaClaw vs OpenClaw)

> Updated after Stage 15 message/channels baseline.

## Practical parity matrix

| Area | OpenClaw | NexaClaw (Stage 15) | Status | Notes |
|---|---|---|---|---|
| HTTP API `/health`, `/api/message`, `/api/status` | ✅ | ✅ | Closed | Stable local gateway |
| Session store | ✅ | ✅ | Closed | File-backed sessions |
| Tools registry + scoped policy | ✅ | ✅ | Closed | global/channel/peer policy |
| Cron jobs (`every`, `at`, `cron`) | ✅ | ⚠️ | Partial (Stage 14 baseline) | added `status/edit/enable/disable/runs`, sessionTarget/payload contracts, run history + retry backoff |
| CLI UX + doctor | ✅ | ✅ | Closed | RU/EN + compatibility dispatcher |
| Setup/onboard/configure wizard | ✅ | ⚠️ | Partial (Stage 13 baseline) | bilingual terminal setup wizard + non-interactive mode |
| Gateway control-plane (`gateway status/start/stop/restart/call`) | ✅ | ⚠️ | Partial (Stage 12 baseline) | local pid/log process management + config.get/apply/patch baseline |
| Security audit (`security audit [--deep] [--fix]`) | ✅ | ⚠️ | Partial (Stage 12 baseline) | dmScope/auth/perms checks + safe fixes |
| Message command/action surface | ✅ | ⚠️ | Partial (Stage 15 baseline) | `message send` (telegram) + strict target validation + dry-run |
| Channels command family | ✅ | ⚠️ | Partial (Stage 15 baseline) | `channels list/status/capabilities/resolve/add/remove` (telegram baseline) |
| Multi-model providers + aliases + fallbacks | ✅ | ✅ | Closed (baseline) | openai/anthropic/openrouter/gemini/minimax |
| Models auth parity beyond API keys | ✅ | ⚠️ | Partial (Stage 11 baseline) | local auth profiles + manual OAuth token setup; no device-code flow yet |
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

Follow the Stage 12 critical path (see `docs/STAGE12_CRITICAL_GAPS.md`):

1. Real OAuth device-code flow (`openai-codex` baseline)
2. Browser backend upgrade (Playwright/CDP) for non-stub snapshot/actions
3. Channels/agents/security deepening (`channels` multi-provider parity, `agents list/add/delete`, deeper security audit)
4. Message action expansion (`react/delete/poll/threads` where supported)
5. Multi-channel/plugin + nodes/canvas ecosystem parity
