# PARITY ROADMAP (NexaClaw vs OpenClaw)

> Updated after Stage 13 onboarding wizard baseline.

## Practical parity matrix

| Area | OpenClaw | NexaClaw (Stage 13) | Status | Notes |
|---|---|---|---|---|
| HTTP API `/health`, `/api/message`, `/api/status` | ✅ | ✅ | Closed | Stable local gateway |
| Session store | ✅ | ✅ | Closed | File-backed sessions |
| Tools registry + scoped policy | ✅ | ✅ | Closed | global/channel/peer policy |
| Cron jobs (`every`, `at`, `cron`) | ✅ | ✅ | Closed (baseline) | CLI + API (`list/add/rm/run/validate`) |
| CLI UX + doctor | ✅ | ✅ | Closed | RU/EN + compatibility dispatcher |
| Setup/onboard/configure wizard | ✅ | ⚠️ | Partial (Stage 13 baseline) | bilingual terminal setup wizard + non-interactive mode |
| Gateway control-plane (`gateway status/start/stop/restart/call`) | ✅ | ⚠️ | Partial (Stage 12 baseline) | local pid/log process management + config.get/apply/patch baseline |
| Security audit (`security audit [--deep] [--fix]`) | ✅ | ⚠️ | Partial (Stage 12 baseline) | dmScope/auth/perms checks + safe fixes |
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

1. Cron semantic parity (`status/edit/enable/disable/runs` + sessionTarget/delivery)
2. Message command baseline (`message send` + strict target validation)
3. Real OAuth device-code flow (`openai-codex` baseline)
4. Browser backend upgrade (Playwright/CDP) for non-stub snapshot/actions
5. Channels/agents/security deepening (`channels status/add/remove`, `agents list/add/delete`, deeper security audit)
