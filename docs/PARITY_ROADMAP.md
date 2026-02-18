# PARITY ROADMAP (NexaClaw vs OpenClaw)

> Updated after Stage 16 browser/oauth/message uplift baseline.

## Practical parity matrix

| Area | OpenClaw | NexaClaw (Stage 16) | Status | Notes |
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
| Models auth parity beyond API keys | ✅ | ⚠️ | Partial (Stage 16 baseline) | `login` import bridge + `auth order`, still no native device-code runtime inside NexaClaw |
| OpenClaw CLI compatibility layer | ✅ | ⚠️ | Partial | implemented core + explicit stubs for not-yet branches |
| Telegram channel baseline | ✅ | ⚠️ | Partial | polling+pairing, no webhooks/sharding |
| Browser relay endpoints | ✅ | ⚠️ | Partial (Stage 16 baseline) | `status/open/navigate/snapshot/click/type/screenshot` via `openclaw_cli` backend bridge |
| Realtime events | ✅ | ✅ | Closed | SSE + EventBus |
| Async task/job lane | ✅ | ⚠️ | Partial | internal queue, single worker |
| Rate limiting + audit trail + recovery | ✅ | ✅ | Closed (baseline) | source RL, JSONL audit, task restore |
| Canvas/Nodes/device control | ✅ | ❌ | Open | not implemented |
| Multi-channel plugins (Discord/Slack/Signal/etc) | ✅ | ❌ | Open | Telegram-first baseline |
| Advanced subagent orchestration | ✅ | ❌ | Open | no external runtime orchestration |

## Next recommended step

After Stage 16, the heaviest remaining parity items are:

1. Native OAuth device-code flow inside NexaClaw (remove dependency on external `openclaw` login/import bridge)
2. Native browser backend (Playwright/CDP in NexaClaw runtime) to reduce external CLI dependency
3. Channels multi-provider expansion (Discord/Slack/Signal/etc) beyond Telegram baseline
4. `agents` / `agent` command-family baseline (list/add/delete/targeted runs)
5. Multi-channel/plugin + nodes/canvas ecosystem parity
