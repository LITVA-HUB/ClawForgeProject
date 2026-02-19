# STAGE 23 — Slice 1: Control-plane parity uplift (cron/gateway/security)

## Goal
Move `cron`, `gateway`, and `security` from baseline-partial toward more practical OpenClaw operator parity while keeping unsupported parts explicit and machine-readable.

## Delivered

### 1) Cron surface uplift
- Added `cron get <id>` / `cron show <id>` in CLI.
- Works in both modes:
  - gateway API path (`GET /api/cron/jobs/<id>`) when available,
  - local scheduler fallback (`CronScheduler::getJob`) otherwise.
- `cron` family now returns structured `not_implemented` JSON for unknown subcommands, with explicit available list.

### 2) Gateway surface uplift
- Added `gateway probe` baseline command:
  - probes local configured gateway (`/health`, `/api/status`),
  - optional explicit target with `--url <http-base-url>`,
  - optional `--no-local`.
- Added gateway run parity alias:
  - `nexaclaw gateway` and `nexaclaw gateway run` map to foreground runtime (`nexaclaw run`).
- Added `gateway call logs.tail` method (JSON response, script-friendly).
- Unsupported gateway subcommands (`discover`, `install`, `uninstall`, unknown) now return structured `not_implemented` JSON with available methods instead of plain text compatibility output.

### 3) Security audit uplift
- Added `gateway.auth.token_strength` warning when token mode is enabled and token length is weak (`<16`).
- Added audit-log file permission checks (`cfg.audit.file`) with `--fix` remediation.
- Unsupported security subcommands now return structured `not_implemented` JSON (`security audit` remains implemented path).

### 4) Smoke coverage
- Added `scripts/smoke_stage23_control_plane_slice1.sh`.
- Included in `scripts/smoke_full.sh`.
- Smoke validates:
  - cron get/show,
  - gateway probe,
  - gateway call logs.tail,
  - structured `not_implemented` responses for unsupported `gateway/security` subcommands.

## Notes / limits
- `gateway probe` is HTTP-baseline probing (not full OpenClaw WS RPC/Bonjour discovery parity).
- `gateway discover/install/uninstall` remain intentionally unimplemented in this slice (explicit stubs).
- Cron editing convenience flags from OpenClaw docs are not in this slice; JSON payload flow remains the authoritative path.
