# PARITY ROADMAP (NexaClaw vs OpenClaw)

> Updated after Stage 23 slice 1 (control-plane cron/gateway/security uplift).

## Practical parity matrix

| Area | OpenClaw | NexaClaw (Stage 21) | Status | Notes |
|---|---|---|---|---|
| HTTP API `/health`, `/api/message`, `/api/status` | ✅ | ✅ | Closed | Stable local gateway |
| Session store | ✅ | ✅ | Closed | File-backed sessions |
| Tools registry + scoped policy | ✅ | ✅ | Closed | global/channel/peer policy |
| Cron jobs (`every`, `at`, `cron`) | ✅ | ⚠️ | Partial (Stage 23 slice 1) | Stage14 semantics + Stage23 `cron get/show`, structured not_implemented for unsupported subcommands |
| CLI UX + doctor | ✅ | ✅ | Closed | RU/EN + compatibility dispatcher |
| Setup/onboard/configure wizard | ✅ | ⚠️ | Partial (Stage 13 baseline) | bilingual terminal setup wizard + non-interactive mode |
| Gateway control-plane (`gateway status/start/stop/restart/call`) | ✅ | ⚠️ | Partial (Stage 23 slice 1) | Stage12 baseline + `gateway`/`gateway run` alias, `gateway probe`, `gateway call logs.tail`, structured stubs for discover/install/uninstall |
| Security audit (`security audit [--deep] [--fix]`) | ✅ | ⚠️ | Partial (Stage 23 slice 1) | dmScope/auth/perms checks + token-strength + audit-log perms + safe fixes |
| Message command/action surface | ✅ | ⚠️ | Partial (Stage 16 baseline) | telegram `send/react/delete/poll` + strict target validation + dry-run |
| Channels command family | ✅ | ⚠️ | Partial (Stage 15 baseline) | `channels list/status/capabilities/resolve/add/remove` (telegram baseline) |
| Agent/agents command family | ✅ | ⚠️ | Partial (Stage 21 baseline) | `list/show/create/delete/use/run` + structured stubs for unsupported subpaths; run path uses tasks/message/session fallback |
| Multi-model providers + aliases + fallbacks | ✅ | ✅ | Closed (baseline) | openai/anthropic/openrouter/gemini/minimax |
| Models auth parity beyond API keys | ✅ | ⚠️ | Partial (Stage 17 slice 2) | Native device-code start/login/poll baseline for `openai-codex` + legacy OpenClaw import bridge fallback |
| OpenClaw CLI compatibility layer | ✅ | ⚠️ | Partial | implemented core + explicit stubs for not-yet branches |
| Telegram channel baseline | ✅ | ⚠️ | Partial | polling+pairing, no webhooks/sharding |
| Browser relay endpoints | ✅ | ⚠️ | Partial (Stage 20 slice 1 uplift) | Native backend now has capability-gated runtime content fetch (`data:` + optional `http(s)` via curl) + structured warnings; `openclaw_cli` remains fallback for full automation |
| Browser admin dashboard | ✅ | ⚠️ | Partial (Stage 19 slice 2 polished) | `/admin` operator console UX (KPIs, refresh controls, denser sessions/cron visibility), enriched `/api/admin/overview`, safe cron quick-actions (`run due`/enable/disable), logs/audit tail |
| Realtime events | ✅ | ✅ | Closed | SSE + EventBus |
| Async task/job lane | ✅ | ⚠️ | Partial | internal queue, single worker |
| Rate limiting + audit trail + recovery | ✅ | ✅ | Closed (baseline) | source RL, JSONL audit, task restore |
| Canvas/Nodes/device control | ✅ | ⚠️ | Partial (Stage 22 slice 1 baseline) | CLI/API seed for `nodes|node|devices|canvas` with local safe registry + structured stubs where runtime unavailable |
| Multi-channel plugins (Discord/Slack/Signal/etc) | ✅ | ❌ | Open | Telegram-first baseline |
| Advanced subagent orchestration | ✅ | ❌ | Open | no external runtime orchestration |

## Next recommended step

After Stage 20 slice 1, the heaviest remaining parity items are:

1. Upgrade native browser backend from runtime-assisted baseline to true Playwright/CDP-attached control
2. Channels multi-provider expansion (Discord/Slack/Signal/etc) beyond Telegram baseline
3. Deepen `agent/agents` from baseline into richer orchestration semantics (subagents, approvals, richer targeting)
4. Multi-channel/plugin expansion beyond Telegram baseline
