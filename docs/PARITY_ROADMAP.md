# PARITY ROADMAP (NexaClaw vs OpenClaw)

> Updated after Stage 30 browser runtime wait/textGone fidelity slice.

## Practical parity matrix

| Area | OpenClaw | NexaClaw (Stage 30) | Status | Notes |
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
| Agent/agents command family | ✅ | ⚠️ | Partial (Stage 29 orchestration uplift) | `list/show/create/delete/use/run/runs` + deterministic run metadata history (`stateDir/agents/runs.jsonl`); run supports additive options (`model/thinking/run-timeout-seconds/cleanup`) with explicit structured fallback errors |
| Multi-model providers + aliases + fallbacks | ✅ | ✅ | Closed (baseline) | openai/anthropic/openrouter/gemini/minimax |
| Models auth parity beyond API keys | ✅ | ⚠️ | Partial (Stage 17 slice 2) | Native device-code start/login/poll baseline for `openai-codex` + legacy OpenClaw import bridge fallback |
| OpenClaw CLI compatibility layer | ✅ | ⚠️ | Partial | implemented core + explicit stubs for not-yet branches |
| Telegram channel baseline | ✅ | ⚠️ | Partial | polling+pairing, no webhooks/sharding |
| Browser relay endpoints | ✅ | ⚠️ | Partial (Stage 30 native wait fidelity uplift) | Stage26+27 added OpenClaw-style `act` envelope at API/CLI/gateway levels (`/api/browser/act`, `browser act --json`, `gateway call browser.act`), with broad `openclaw_cli` dispatch. Stage28 added constrained native `act.evaluate`; Stage30 upgrades native `act.wait` from no-op to real wait lifecycle (`timeMs`, `text`, `textGone`, timeout polling) and explicit structured capability-gated errors for unsupported wait contracts (`selector/url/loadState/fn`) |
| Browser admin dashboard | ✅ | ⚠️ | Partial (Stage 19 slice 2 polished) | `/admin` operator console UX (KPIs, refresh controls, denser sessions/cron visibility), enriched `/api/admin/overview`, safe cron quick-actions (`run due`/enable/disable), logs/audit tail |
| Realtime events | ✅ | ✅ | Closed | SSE + EventBus |
| Async task/job lane | ✅ | ⚠️ | Partial | internal queue, single worker |
| Rate limiting + audit trail + recovery | ✅ | ✅ | Closed (baseline) | source RL, JSONL audit, task restore |
| Canvas/Nodes/device control | ✅ | ⚠️ | Partial (Stage 25 slice 2 practical uplift) | CLI/API surface now includes safe local runtime probe/invoke for nodes/devices and virtual canvas snapshot/invoke with capability-gated structured errors when runtime unavailable |
| Multi-channel plugins (Discord/Slack/Signal/etc) | ✅ | ❌ | Open | Telegram-first baseline |
| Advanced subagent orchestration | ✅ | ❌ | Open | no external runtime orchestration |

## Next recommended step

After Stage 20 slice 1, the heaviest remaining parity items are:

1. Upgrade native browser backend from runtime-assisted baseline to true Playwright/CDP-attached control
2. Channels multi-provider expansion (Discord/Slack/Signal/etc) beyond Telegram baseline
3. Extend agent orchestration toward true subagent lifecycle control (stop/log/info/send parity)
4. Multi-channel/plugin expansion beyond Telegram baseline
