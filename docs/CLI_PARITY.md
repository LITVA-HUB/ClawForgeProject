# CLI Parity Matrix (OpenClaw docs audit -> NexaClaw Stage 34 runtime control slice)

Source audit: `/opt/homebrew/lib/node_modules/openclaw/docs/cli/*.md`

## Legend
- **implemented** — command works in NexaClaw CLI now
- **partial** — command exists, but reduced behavior / baseline only
- **stub** — compatibility branch exists and returns explicit `not implemented yet`
- **impossible-now** — not realistically implementable in current NexaClaw scope (requires OpenClaw-native infra/UI/ecosystem)

## Top-level parity (from OpenClaw CLI docs)

| OpenClaw top-level | Key subcommands in OpenClaw docs | NexaClaw status | NexaClaw mapping / note |
|---|---|---|---|
| `status` | `status` | implemented | `nexaclaw status` |
| `health` | `health` | implemented | `nexaclaw health` |
| `doctor` | `doctor` | implemented | `nexaclaw doctor` / `--doctor` |
| `sessions` | list/show/ops | partial | `nexaclaw sessions` list baseline |
| `cron` | status/list/add/edit/enable/disable/run/runs/validate/rm (+get/show baseline) | partial | Stage 23 slice 1 adds `get/show` + structured not_implemented for unsupported subcommands; still not full OpenClaw delivery ecosystem |
| `tools` | list/call | implemented | `tools list`, `tools call <name> --json <payload>` |
| `browser` | status/open/snapshot/... | partial | Stage 18 baseline: `status|open|navigate|snapshot|click|type|screenshot` via native backend (`browser.backend=native`) with `openclaw_cli` fallback/compat |
| `config` | get/set | partial | expanded key coverage, not full OpenClaw config surface |
| `models` | list/status/set/aliases/fallbacks/probe/auth | implemented | plus `set-image`, auth profiles, `auth login` import bridge, `auth order` |
| `image-fallbacks` | list/add/remove/clear | implemented | added as baseline config ops |
| `logs` | tail/... | partial | `logs tail [lines]` (audit file tail) |
| `system` | event/... | partial | `system event <text>` baseline enqueue path |
| `pairing` | list/approve/... | partial | `list`, `approve` |
| `gateway` | status/start/stop/restart/call/probe/discover/... | partial | Stage 23 slice 1: `gateway`/`gateway run` alias to foreground run, added `probe`, `gateway call logs.tail`, and structured not_implemented for discover/install/uninstall |
| `message` | send/... | partial | Stage 16 baseline: telegram `send|react|delete|poll` with strict target validation + dry-run |
| `agent` | manage/ops | partial | Stage 34 orchestration+runtime-control alias to `agents` family (`list/show/create/update/delete/use/run/runs/run-status`) + runtime context/tool policy enforcement + structured task run-events API (`/api/tasks/{id}/events`) + structured stubs for unavailable subpaths |
| `agents` | manage/ops | partial | Stage 34 advanced orchestration (`list/show/create/update/delete/use/run/runs/run-status`) with file-backed registry, subagent/context/tool policy fields, deterministic run lifecycle history, runtime task-path enforcement (`context` carryover/history + `/tool` allow/deny checks), and richer task-lane transitions/events (`cancelling`, run events introspection) |
| `acp` | protocol tooling | impossible-now | requires ACP ecosystem parity |
| `approvals` | approvals workflow | stub | not mapped yet |
| `channels` | channel providers mgmt | partial | telegram baseline: list/status/capabilities/resolve/add/remove |
| `dashboard` | dashboard/ui | impossible-now | OpenClaw UI stack |
| `devices` | devices control | partial | Stage 25 slice 2 practical uplift: `list|status|invoke` with read-safe local runtime probe and structured capability gates |
| `directory` | identity/directory | impossible-now | no directory backend in NexaClaw |
| `dns` | dns ops | stub | not mapped yet |
| `docs` | docs tooling | stub | not mapped yet |
| `hooks` | hooks ops | stub | not mapped yet |
| `memory` | memory tooling | stub | not mapped yet |
| `node` | node mgmt | partial | Stage 25 slice 2 alias to `nodes` practical runtime baseline (`list|status|describe|invoke`) |
| `nodes` | nodes mgmt | partial | Stage 25 slice 2 practical baseline (`list|status|describe|invoke`) with safe local registry + local runtime probe |
| `onboard` | onboarding flow | partial | `onboard` maps to bilingual setup wizard baseline |
| `plugins` | plugin ops | stub | not mapped yet |
| `reset` | reset ops | stub | not mapped yet |
| `sandbox` | sandbox ops | stub | not mapped yet |
| `security` | security audit | partial | `security audit [--deep] [--fix]` baseline checks (dm scope, auth env, perms) |
| `setup` | setup flow | partial | terminal setup wizard (`--wizard` / `--non-interactive`) |
| `skills` | skills mgmt | stub | not mapped yet |
| `tui` | terminal UI | impossible-now | no TUI subsystem |
| `uninstall` | uninstall flow | impossible-now | external installer/runtime concern |
| `update` | self-update | impossible-now | no packaged updater pipeline |
| `voicecall` | voice call controls | impossible-now | no voice-call runtime |
| `webhooks` | webhooks ops | stub | not mapped yet |
| `configure` | setup/config assistant | partial | `configure` maps to the same bilingual setup wizard baseline |
| `dashboard` | UI | impossible-now | see above |

## Key subcommand parity details

### Browser (Stage 26 source-driven act parity slice)
- `browser status` — **implemented**
- `browser open <url>` — **implemented**
- `browser navigate <url>` — **implemented**
- `browser snapshot [urlHint]` — **implemented baseline** (native diagnostic snapshot in `browser.backend=native`; `openclaw_cli` bridge still supported)
- `browser click <ref>` — **implemented baseline**
- `browser type <ref> <text>` — **implemented baseline**
- `browser screenshot [targetId]` — **implemented baseline**
- `browser act --json '{...}' [--target-id <id>]` — **implemented partial**
- stage24 slice2 uplift: native runtime now parses basic HTML form context and models GET-form submission side effects (`type --submit` and submit-control `click`) into navigation URLs, closer to real automation runtime behavior
- stage24 capability gates: non-GET form submits return structured `native_capability_form_method_unsupported` errors with machine-readable capability metadata; non-text `type` calls return structured `native_type_ref_not_text_input`
- prior native parity improvements retained: stable target lifecycle (`activeTargetId`), deterministic ref stability across snapshots, type/click state effects visible in snapshot flow, safer persisted state writes, runtime-aware loading for `data:` + `http(s)` (via `curl`) with structured warning codes
- stage26 parity slice: added `/api/browser/act`, `gateway call browser.act`, and CLI `browser act --json` with OpenClaw-compatible request envelope (`request.kind/ref/text/...`)
- stage27 parity slice expanded `act` kinds toward OpenClaw (`click,type,press,hover,scrollIntoView,drag,select,fill,resize,wait,evaluate,close` envelope coverage)
- stage28 realism slice adds native `act.evaluate` via a constrained JS runtime model (`location`, `document.title`, optional element model for `ref`) with deterministic state mutation plumbing
- native `act` now supports `click`, `type`, `press` (Enter-only with explicit non-Enter capability error), `wait` (real `timeMs`/`text`/`textGone` polling lifecycle with timeout errors), `close`, `hover`, `scrollIntoView`, `fill`, `resize`, and `evaluate` (sync-only)
- Stage30 parity uplift: native `act.wait` returns structured capability-gated errors for unsupported Playwright wait selectors (`selector`, `url`, `loadState`, `fn`) instead of pretending success
- native unsupported runtime kinds (`drag/select`) return structured capability-gated errors (`native_capability_kind_unsupported`) instead of silent behavior
- native evaluate async paths are explicitly capability-gated with structured `native_capability_evaluate_async_unsupported`
- openclaw_cli backend now dispatches most OpenClaw `act` kinds directly to corresponding `openclaw browser <cmd>` commands; unsupported kinds still return explicit structured errors (`openclaw_cli_act_kind_unsupported_in_nexaclaw`)
- limitation: native backend is still not full CDP/Playwright control (no full JS runtime/CDP sessions); use `browser.backend=openclaw_cli` when real OpenClaw browser automation is required

### Cron (Stage 23 control-plane uplift on top of Stage 14 baseline)
- `cron status` — **implemented**
- `cron list` — **implemented**
- `cron get <id>` / `cron show <id>` — **implemented baseline**
- `cron add --json '<payload>'` — **implemented**
- `cron edit <id> --json '<patch>'` — **implemented**
- `cron enable <id>` / `cron disable <id>` — **implemented**
- `cron run <id> [--due]` — **implemented** (`force|due` semantics)
- `cron runs <id> --limit <n>` — **implemented** (JSONL run history)
- `cron rm <id>` — **implemented**
- `cron validate --json '<payload>'` — **implemented**
- contract checks implemented: `main->systemEvent`, `isolated->agentTurn`
- default isolated `delivery.mode=announce`; retry backoff ladder after recurring errors

### Message (Stage 16 baseline)
- `message send --channel telegram --target <...> --message <...>` — **implemented baseline**
- `message react --channel telegram --target <...> --message-id <id> --emoji <...>` — **implemented baseline**
- `message delete --channel telegram --target <...> --message-id <id>` — **implemented baseline**
- `message poll --channel telegram --target <...> --poll-question <...> --poll-option ...` — **implemented baseline**
- strict telegram target validation (`@username`, `chatId`, `chatId:topic:threadId`)
- `--dry-run` supported for safe validation across actions
- non-telegram channels are still roadmap

### Channels (Stage 15 baseline)
- `channels list` / `channels status` — **implemented baseline**
- `channels capabilities` — **implemented baseline** (telegram static capability map)
- `channels resolve --channel telegram <target>` — **implemented baseline**
- `channels add/remove --channel telegram` — **implemented baseline** (config toggles)

### Nodes / Node / Devices / Canvas (Stage 25 slice 2 practical uplift)
- `nodes list|status|describe|invoke` — **implemented practical baseline** (local registry + runtime metadata + safe local probe invoke)
- `node ...` — **implemented alias** of `nodes` with same runtime behavior
- `devices list|status|invoke` — **implemented practical baseline** (mapped from nodes + safe local runtime metrics invoke)
- `canvas status|list` — **implemented practical baseline** (runtime capability status, supported actions, safe-mode metadata)
- `canvas snapshot|invoke` — **implemented practical baseline** (virtual snapshot + modeled invoke actions in read-safe mode; structured unavailable/action-not-allowed errors retained)
- `gateway call` parity expanded: `nodes.*`, `devices.list|status|invoke`, `canvas.status|snapshot|invoke`
- safety contract: invoke paths remain read-safe only with explicit capability-gated structured errors when runtime is unavailable

### Agent / Agents (Stage 31 advanced orchestration uplift)
- `agents list` — **implemented baseline**
- `agents show <id>` — **implemented baseline**
- `agents create <id> [--name] [--session-key]` — **implemented baseline**
- `agents update <id>|--agent <id> [--name] [--session-key] [--profile] [--description] [--role] [--tags <csv>] [--subagent-model] [--subagent-thinking] [--allow-agents <csv|*>] [--max-concurrent <n>] [--archive-after-minutes <n>] [--context-history-limit <n>] [--context-carryover <inherit|minimal|none>] [--tool-allow <csv|*>] [--tool-deny <csv>]` — **implemented uplift**
- `agents delete <id>` — **implemented baseline** (`main` protected)
- `agents use <id>` — **implemented baseline** (active pointer)
- `agents run [<id>|--agent <id>] --message <text> [--timeout-ms <ms>] [--model <name>] [--thinking <level>] [--run-timeout-seconds <s>] [--cleanup <keep|delete>] [--context-history-limit <n>] [--context-carryover <inherit|minimal|none>] [--tool-allow <csv|*>] [--tool-deny <csv>]` — **implemented uplift**
- `agents runs [<id>|--agent <id>] [--limit <n>] [--status <state>] [--active]` — **implemented uplift**
- `agents run-status <run-id>|--run-id <id> [--agent <id>]` — **implemented uplift**
- `agent ...` — **implemented baseline alias family**
- unsupported subpaths return structured JSON `not_implemented` stubs

Orchestration path for `run`:
1) `/api/tasks` (preferred, carries advanced run options where available),
2) `/api/message` fallback,
3) local session append fallback for baseline fields only.

Deterministic run metadata history:
- run records persisted to `stateDir/agents/runs.jsonl`
- `agents update` now stores additive per-agent context/tool policy metadata (`context.historyLimit`, `context.carryover`, `tools.allow`, `tools.deny`) with normalization and backward compatibility
- `agents run` carries context/tool policy into `run.options` and gateway task payload when available; local fallback returns structured `advanced_options_require_gateway`
- Stage33: gateway task path now enforces runtime `context` policy (history/carryover pruning) and `tools` policy (`deny` precedence + allowlist behavior) for `/tool` command execution
- Stage33: `/api/tasks` validates policy payloads with explicit JSON errors (`invalid_context_*`, `invalid_tools_policy`, `invalid_tool_allow`, `invalid_tool_deny`)
- records now include run lifecycle metadata (`createdAtMs`, `startedAtMs`, optional `endedAtMs`, `state`, `terminal`)
- `agents runs`/`agents history` list per-agent run outcomes with optional status/active filtering
- `agents run-status` resolves a run record deterministically by `runId`
- gateway-unavailable + advanced options returns explicit structured error (`advanced_options_require_gateway`)

### Tools
- `tools list` — **implemented**
- `tools call <name> --json '<payload>'` — **implemented**

### Config get/set (covered keys)
- **implemented**: `gateway.auth.mode`, `gateway.auth.tokenEnv`, `api.dmScope`, `telegram.dmPolicy`, `models.routing.current`, `models.routing.image`
- **partial**: objects/arrays (`models.routing.aliases`, `models.routing.fallbacks`, `models.routing.imageFallbacks`) can be read, but set via dedicated commands (`models aliases...`, `models fallbacks...`, `image-fallbacks...`)

### Models
- `models list|status|set|aliases|fallbacks` — **implemented**
- `models probe` — **implemented** (shows auth source env/profile, no token-spend calls)
- `models set-image` — **implemented** (config op baseline)
- `models auth list|add|paste-token|setup-token|login|use|remove` — **implemented baseline**
- `models auth order get|set|clear` — **implemented baseline**
- login note: `models auth login --provider openai-codex` now supports native device-code start/poll baseline (`--device-code-json` optional; native start when omitted) with OpenClaw bridge available via `--bridge-import` (or legacy bridge flags)
- `image-fallbacks list|add|remove|clear` — **implemented**

### Logs/System
- `logs tail [lines]` — **implemented** (audit file tail)
- `system event <text>` — **implemented** (API path when available, local fallback to main session system message)

### Gateway (Stage 23 control-plane uplift on top of Stage 12 baseline)
- `gateway` / `gateway run` — **implemented baseline alias** to foreground `run`
- `gateway status|start|stop|restart|health` — **partial** (single-host local process baseline with pid/log files)
- `gateway probe [--url <http-base>] [--no-local]` — **implemented baseline** (HTTP probe for `/health` + `/api/status`)
- `gateway call config.get|config.apply|config.patch|logs.tail` — **partial** (local RPC-like flow + config hash guard + validation + audit tail)
- unsupported `gateway discover|install|uninstall` — explicit structured JSON `not_implemented`
- `gateway call update.run` — explicit `not implemented yet`

### Security (Stage 23 control-plane uplift on top of Stage 12 baseline)
- `security audit` — **partial**
- checks: DM scope risk, gateway auth env presence, gateway token weak-length warning, config/state/audit-log permissions
- `security audit --fix` applies safe baseline remediations (dmScope + perms)
- unsupported security subcommands return structured JSON `not_implemented`

### Setup/Onboard/Configure (Stage 13 wizard baseline)
- `setup` / `onboard` / `configure` — **partial**
- bilingual terminal wizard (RU/EN) with interactive menu
- non-interactive mode via `--non-interactive` / `--yes`
- safe defaults preset (dmScope + auth token env + telegram dmPolicy)

## Stage 16 summary
- Focus for this sprint: close three heavy practical gaps — browser actions, OAuth login bridge/import, and message action surface.
- Added `openclaw_cli` browser backend bridge (`navigate/click/type/screenshot`), `models auth login` + `auth order`, and telegram `message react/delete/poll` baseline.
- Where full OpenClaw feature equivalence is still impossible now, NexaClaw returns explicit diagnostics and scope limits instead of silent gaps.

### OAuth parity note
- Native NexaClaw device-code start/login/poll baseline is implemented for `openai-codex`.
- OpenClaw login/import bridge remains available for backward compatibility (`--bridge-import`).

### Installer parity note
- Added `scripts/install.sh` one-command installer (clone/update/build/install).
- Supports `--dry-run`, `--pin-commit`, and `--system` for safer operational usage.

## Stage 17 summary (slice 2)
- Added native OAuth device-code START acquisition endpoint flow in NexaClaw for `models auth login --provider openai-codex` when `--device-code-json` is omitted.
- Added practical start flags/envs: `--device-start-url` / `OPENAI_CODEX_DEVICE_START_URL`, `--client-id` / `OPENAI_CODEX_CLIENT_ID`, `--scope` / `OPENAI_CODEX_SCOPE`.
- Existing poll/store flow remains intact (`--poll`, `--token-url`), now interoperable with native start in the same command.
- Legacy OpenClaw import path kept for compatibility and can be forced with `--bridge-import` (or legacy bridge flags).
- Start and poll paths return explicit structured JSON for success/error automation.
