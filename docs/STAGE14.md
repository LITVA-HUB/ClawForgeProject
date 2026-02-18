# Stage 14 — Cron semantic parity uplift

## Goal

Move cron from basic scheduler CRUD to OpenClaw-like operational semantics:

- richer job schema (`sessionTarget` + `payload` + `delivery` + `wakeMode`)
- lifecycle controls (`status/edit/enable/disable/runs`)
- run history and retry behavior

## Delivered

### CLI commands

Added/expanded:

- `nexaclaw cron status`
- `nexaclaw cron list`
- `nexaclaw cron add --json '{...}'`
- `nexaclaw cron edit <id> --json '{...patch...}'`
- `nexaclaw cron enable <id>`
- `nexaclaw cron disable <id>`
- `nexaclaw cron run <id> [--due]`
- `nexaclaw cron runs <id> [--limit N]`
- `nexaclaw cron validate --json '{...}'`
- `nexaclaw cron rm <id>`

### Cron schema uplift

Job model now supports:

- `schedule.kind`: `at|every|cron`
- `sessionTarget`: `main|isolated`
- `payload.kind`: `systemEvent|agentTurn`
- `wakeMode`: `now|next-heartbeat`
- `delivery.mode`: `none|announce`
- `agentId`, `description`, `deleteAfterRun`, `enabled`

Compatibility behavior:

- legacy `sessionKey` and `message` are still accepted and serialized
- mixed payloads are normalized (`text` / `message`)

### Contract validation

Enforced constraints (OpenClaw-like):

- `sessionTarget=main` requires `payload.kind=systemEvent`
- `sessionTarget=isolated` requires `payload.kind=agentTurn`
- `delivery` is isolated-focused (`main` can only keep `mode=none`)

### Runtime semantics

- Main jobs:
  - `wakeMode=now` -> immediate system event path
  - `wakeMode=next-heartbeat` -> queued into main session as system message
- Isolated jobs:
  - dedicated cron session key (fresh per run by default)
  - optional announce summary back to main session

### Reliability

- Run history persisted in JSONL:
  - `state/cron/runs/<jobId>.jsonl`
- Recorded statuses:
  - `ok`, `error`, `skipped`
- Manual run modes:
  - `force` (default)
  - `due` (skip if not due)
- Retry backoff after recurring failures:
  - `30s -> 1m -> 5m -> 15m -> 60m`

### API surface expansion

Added endpoints:

- `GET /api/cron/status`
- `PATCH /api/cron/jobs/:id`
- `POST /api/cron/jobs/:id/enable`
- `POST /api/cron/jobs/:id/disable`
- `POST /api/cron/jobs/:id/run` (`mode: force|due`)
- `GET /api/cron/jobs/:id/runs?limit=N`

Existing endpoints retained for compatibility:

- `/api/cron/jobs` list/add
- `/api/cron/validate`
- `/api/cron/jobs/:id/run-now`
- `/api/cron/jobs/:id` delete

## Smoke coverage

Added:

- `scripts/smoke_stage14_cron_semantics.sh`
- wired into `scripts/smoke_full.sh`

Checks:

- status/validate success + contract failure case
- add/edit/enable/disable
- run `--due` skip semantics
- run force success
- runs history retrieval
- isolated add default `delivery.mode=announce`

## Notes

This closes the major cron semantics gap from Stage 12 baseline while preserving compatibility-oriented local fallback behavior.
