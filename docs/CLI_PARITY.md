# CLI Parity Matrix (OpenClaw -> ClawForge Stage 9)

## Legend
- **implemented**: usable command in ClawForge now
- **partial**: command exists with reduced behavior
- **stub**: compatibility dispatcher exists, returns `not implemented yet` with this doc reference

## Top-level parity

| OpenClaw command | ClawForge | Status | Notes |
|---|---|---|---|
| `status` | `clawforge status` | implemented | API + local fallback |
| `health` | `clawforge health` | implemented | checks `/health` |
| `sessions` | `clawforge sessions` | implemented | API + local fallback |
| `cron` | `clawforge cron list` | partial | only `list` in CLI (API has more) |
| `tools` | `clawforge tools list` | partial | only list |
| `models` | `clawforge models ...` | implemented | list/status/set + aliases/fallbacks |
| `pairing` | `clawforge pairing list|approve` | implemented | baseline |
| `doctor` | `clawforge --doctor` or `clawforge doctor` | implemented | baseline diagnostics |
| `config` | `clawforge config get/set` | partial | baseline keys only |
| `gateway` | compat stub | stub | planned |
| `browser` | compat stub | stub | HTTP endpoints exist, CLI branch not implemented |
| `setup`, `onboard`, `configure`, `dashboard`, `reset`, `uninstall`, `update`, `message`, `agent`, `agents`, `acp`, `logs`, `system`, `memory`, `nodes`, `devices`, `node`, `approvals`, `sandbox`, `dns`, `docs`, `hooks`, `webhooks`, `plugins`, `channels`, `security`, `skills`, `tui` | compat stub | stub | clean diagnostic instead of hard unknown |

## Models command parity

| OpenClaw models subcommand | ClawForge | Status |
|---|---|---|
| `models list` | `models list` | implemented |
| `models status` | `models status` | implemented |
| `models set` | `models set <provider/model or alias>` | implemented |
| `models aliases list/add/remove` | same | implemented |
| `models fallbacks list/add/remove/clear` | same | implemented |
| `models set-image`, `image-fallbacks`, `scan`, `auth ...` | n/a | stub (future) |

## Notes
- Compatibility dispatcher intentionally catches many OpenClaw top-level commands and returns explicit stub text.
- This avoids generic `unknown command` and makes migration friendlier.
