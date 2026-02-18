# CLI Parity Matrix (OpenClaw docs audit -> NexaClaw Stage 15)

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
| `cron` | status/list/add/edit/enable/disable/run/runs/validate/rm | partial | Stage 14 semantic baseline (sessionTarget/payload/delivery/wakeMode + run history), still not full OpenClaw delivery ecosystem |
| `tools` | list/call | implemented | `tools list`, `tools call <name> --json <payload>` |
| `browser` | status/open/snapshot/... | partial | `status|open|snapshot` implemented; snapshot still diagnostic baseline |
| `config` | get/set | partial | expanded key coverage, not full OpenClaw config surface |
| `models` | list/status/set/aliases/fallbacks/probe/auth | implemented | plus `set-image`, auth profiles |
| `image-fallbacks` | list/add/remove/clear | implemented | added as baseline config ops |
| `logs` | tail/... | partial | `logs tail [lines]` (audit file tail) |
| `system` | event/... | partial | `system event <text>` baseline enqueue path |
| `pairing` | list/approve/... | partial | `list`, `approve` |
| `gateway` | status/start/stop/restart/call | partial | `status|start|stop|restart|health|call` baseline (local pid/log + RPC-like `gateway call`) |
| `message` | send/... | partial | `message send` baseline for telegram with strict target validation + dry-run |
| `agent` | manage/ops | stub | not mapped yet |
| `agents` | manage/ops | stub | not mapped yet |
| `acp` | protocol tooling | impossible-now | requires ACP ecosystem parity |
| `approvals` | approvals workflow | stub | not mapped yet |
| `channels` | channel providers mgmt | partial | telegram baseline: list/status/capabilities/resolve/add/remove |
| `dashboard` | dashboard/ui | impossible-now | OpenClaw UI stack |
| `devices` | devices control | stub | not mapped yet |
| `directory` | identity/directory | impossible-now | no directory backend in NexaClaw |
| `dns` | dns ops | stub | not mapped yet |
| `docs` | docs tooling | stub | not mapped yet |
| `hooks` | hooks ops | stub | not mapped yet |
| `memory` | memory tooling | stub | not mapped yet |
| `node` | node mgmt | stub | not mapped yet |
| `nodes` | nodes mgmt | stub | not mapped yet |
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

### Browser
- `browser status` — **implemented**
- `browser open <url>` — **implemented**
- `browser snapshot [urlHint]` — **partial** (currently diagnostic/baseline in backend)

### Cron (Stage 14 semantic baseline)
- `cron status` — **implemented**
- `cron list` — **implemented**
- `cron add --json '<payload>'` — **implemented**
- `cron edit <id> --json '<patch>'` — **implemented**
- `cron enable <id>` / `cron disable <id>` — **implemented**
- `cron run <id> [--due]` — **implemented** (`force|due` semantics)
- `cron runs <id> --limit <n>` — **implemented** (JSONL run history)
- `cron rm <id>` — **implemented**
- `cron validate --json '<payload>'` — **implemented**
- contract checks implemented: `main->systemEvent`, `isolated->agentTurn`
- default isolated `delivery.mode=announce`; retry backoff ladder after recurring errors

### Message (Stage 15 baseline)
- `message send --channel telegram --target <...> --message <...>` — **implemented baseline**
- strict telegram target validation (`@username`, `chatId`, `chatId:topic:threadId`)
- `--dry-run` supported for safe validation
- non-telegram channels/actions still roadmap

### Channels (Stage 15 baseline)
- `channels list` / `channels status` — **implemented baseline**
- `channels capabilities` — **implemented baseline** (telegram static capability map)
- `channels resolve --channel telegram <target>` — **implemented baseline**
- `channels add/remove --channel telegram` — **implemented baseline** (config toggles)

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
- `models auth list|add|paste-token|setup-token|use|remove` — **implemented baseline** (local token store, manual OAuth token helper)
- `image-fallbacks list|add|remove|clear` — **implemented**

### Logs/System
- `logs tail [lines]` — **implemented** (audit file tail)
- `system event <text>` — **implemented** (API path when available, local fallback to main session system message)

### Gateway (Stage 12 baseline)
- `gateway status|start|stop|restart|health` — **partial** (single-host local process baseline with pid/log files)
- `gateway call config.get|config.apply|config.patch` — **partial** (local RPC-like flow + config hash guard + validation)
- `gateway call update.run` — explicit `not implemented yet`

### Security (Stage 12 baseline)
- `security audit` — **partial**
- checks: DM scope risk, gateway auth env presence, config/state permissions
- `security audit --fix` applies safe baseline remediations (dmScope + perms)

### Setup/Onboard/Configure (Stage 13 wizard baseline)
- `setup` / `onboard` / `configure` — **partial**
- bilingual terminal wizard (RU/EN) with interactive menu
- non-interactive mode via `--non-interactive` / `--yes`
- safe defaults preset (dmScope + auth token env + telegram dmPolicy)

## Stage 15 summary
- Focus for this sprint: message/channels baseline on top of Stage 14 cron semantics.
- Added strict telegram send path and channel-management baseline commands.
- Where full OpenClaw feature equivalence is impossible now, NexaClaw returns explicit baseline diagnostics instead of silent gaps.


### OAuth parity note
- `models auth setup-token --provider openai-codex` is a **manual token setup baseline**.
- Device-code OAuth flow is not implemented yet; CLI prints an explicit warning and stores only user-provided token.

### Installer parity note
- Added `scripts/install.sh` one-command installer (clone/update/build/install).
- Supports `--dry-run`, `--pin-commit`, and `--system` for safer operational usage.