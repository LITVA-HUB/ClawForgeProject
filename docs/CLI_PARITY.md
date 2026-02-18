# CLI Parity Matrix (OpenClaw docs audit -> ClawForge Stage 11)

Source audit: `/opt/homebrew/lib/node_modules/openclaw/docs/cli/*.md`

## Legend
- **implemented** — command works in ClawForge CLI now
- **partial** — command exists, but reduced behavior / baseline only
- **stub** — compatibility branch exists and returns explicit `not implemented yet`
- **impossible-now** — not realistically implementable in current ClawForge scope (requires OpenClaw-native infra/UI/ecosystem)

## Top-level parity (from OpenClaw CLI docs)

| OpenClaw top-level | Key subcommands in OpenClaw docs | ClawForge status | ClawForge mapping / note |
|---|---|---|---|
| `status` | `status` | implemented | `clawforge status` |
| `health` | `health` | implemented | `clawforge health` |
| `doctor` | `doctor` | implemented | `clawforge doctor` / `--doctor` |
| `sessions` | list/show/ops | partial | `clawforge sessions` list baseline |
| `cron` | list/add/rm/run/validate | implemented | all baseline ops in CLI + local fallback |
| `tools` | list/call | implemented | `tools list`, `tools call <name> --json <payload>` |
| `browser` | status/open/snapshot/... | partial | `status|open|snapshot` implemented; snapshot still diagnostic baseline |
| `config` | get/set | partial | expanded key coverage, not full OpenClaw config surface |
| `models` | list/status/set/aliases/fallbacks/probe/auth | implemented | plus `set-image`, auth profiles |
| `image-fallbacks` | list/add/remove/clear | implemented | added as baseline config ops |
| `logs` | tail/... | partial | `logs tail [lines]` (audit file tail) |
| `system` | event/... | partial | `system event <text>` baseline enqueue path |
| `pairing` | list/approve/... | partial | `list`, `approve` |
| `gateway` | status/start/stop/restart/... | stub | not mapped yet |
| `message` | send/... | stub | not mapped yet |
| `agent` | manage/ops | stub | not mapped yet |
| `agents` | manage/ops | stub | not mapped yet |
| `acp` | protocol tooling | impossible-now | requires ACP ecosystem parity |
| `approvals` | approvals workflow | stub | not mapped yet |
| `channels` | channel providers mgmt | stub | not mapped yet |
| `dashboard` | dashboard/ui | impossible-now | OpenClaw UI stack |
| `devices` | devices control | stub | not mapped yet |
| `directory` | identity/directory | impossible-now | no directory backend in ClawForge |
| `dns` | dns ops | stub | not mapped yet |
| `docs` | docs tooling | stub | not mapped yet |
| `hooks` | hooks ops | stub | not mapped yet |
| `memory` | memory tooling | stub | not mapped yet |
| `node` | node mgmt | stub | not mapped yet |
| `nodes` | nodes mgmt | stub | not mapped yet |
| `onboard` | onboarding flow | stub | not mapped yet |
| `plugins` | plugin ops | stub | not mapped yet |
| `reset` | reset ops | stub | not mapped yet |
| `sandbox` | sandbox ops | stub | not mapped yet |
| `security` | security policies | stub | not mapped yet |
| `setup` | setup flow | stub | not mapped yet |
| `skills` | skills mgmt | stub | not mapped yet |
| `tui` | terminal UI | impossible-now | no TUI subsystem |
| `uninstall` | uninstall flow | impossible-now | external installer/runtime concern |
| `update` | self-update | impossible-now | no packaged updater pipeline |
| `voicecall` | voice call controls | impossible-now | no voice-call runtime |
| `webhooks` | webhooks ops | stub | not mapped yet |
| `configure` | setup/config assistant | stub | not mapped yet |
| `dashboard` | UI | impossible-now | see above |

## Key subcommand parity details

### Browser
- `browser status` — **implemented**
- `browser open <url>` — **implemented**
- `browser snapshot [urlHint]` — **partial** (currently diagnostic/baseline in backend)

### Cron
- `cron list` — **implemented**
- `cron add --json '<payload>'` — **implemented**
- `cron rm <id>` — **implemented**
- `cron run <id>` — **implemented**
- `cron validate --json '<payload>'` — **implemented**

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

## Stage 11 summary
- Focus for this sprint: deep practical parity on frequently-used operational CLI branches.
- Where full OpenClaw feature equivalence is impossible now, ClawForge returns explicit baseline diagnostics instead of silent gaps.


### OAuth parity note
- `models auth setup-token --provider openai-codex` is a **manual token setup baseline**.
- Device-code OAuth flow is not implemented yet; CLI prints an explicit warning and stores only user-provided token.