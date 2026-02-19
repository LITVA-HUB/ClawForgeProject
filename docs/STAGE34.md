# STAGE 34 — Agents runtime control depth (slice 3): task run events + richer transitions

## Goal
Implement one concrete, high-impact runtime control feature beyond Stage33 with additive compatibility: **JSON-first run events introspection** and **richer task status transitions** for agent task-lane runs.

## OpenClaw source refs inspected
- `/opt/homebrew/lib/node_modules/openclaw/docs/tools/subagents.md`
  - run inspection/control semantics (`list|stop|log|info|send`)
  - emphasis on lifecycle observability for background runs
- `/opt/homebrew/lib/node_modules/openclaw/docs/tools/slash-commands.md`
  - `/subagents list|stop|log|info|send` and `/stop` control surface intent
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/pi-embedded-runner/types.d.ts`
  - run stop reason semantics for structured lifecycle reporting

## What was implemented

### 1) Runtime task events ledger (persistent)
`TaskQueue` now stores per-task lifecycle events in a bounded persistent ledger:
- event shape: `{ seq, type, status, atMs, details? }`
- persisted in `stateDir/tasks/tasks.json` under each task row
- bounded retention per task (256 newest events)

Generated events include:
- `task_enqueued`
- `task_started`
- `task_cancelling` / `task_cancelled` / `task_cancel_ignored`
- `task_finished`
- `task_recovered` (startup recovery for interrupted running tasks)

### 2) Richer status transitions for live control
Task statuses now support additive intermediate state:
- `queued -> running -> cancelling -> cancelled|done|failed|timeout`

`POST /api/tasks/{id}/cancel` behavior:
- `queued`: immediate terminal cancel
- `running`: moves to `cancelling` + cooperative cancel flag
- `cancelling`: idempotent/ignored marker event
- terminal states: explicit structured error
  - `error: "task_cancel_not_allowed"`
  - includes `terminal: true` and `allowedStatuses`

### 3) New JSON-first run events API
Added additive endpoint:
- `GET /api/tasks/{id}/events?limit=<1..500>&afterSeq=<>=0`

Returns structured payload:
- `ok`, `taskId`, `status`, `events[]`, `afterSeq`, `nextAfterSeq`, `lastEventSeq`

Validation errors are explicit JSON:
- `invalid_events_limit`
- `invalid_events_after_seq`

### 4) Compatibility
- Existing `/api/tasks`, `/api/tasks/{id}`, `/api/tasks/{id}/cancel` remain stable.
- Existing consumers of prior terminal statuses continue to work.
- Additive fields in task JSON (`eventsCount`, `lastEventSeq`) do not break old clients.

## Smoke coverage
Added:
- `scripts/smoke_stage34_agents_runtime_events.sh`

Checks:
- enqueue/run/finish lifecycle event visibility via `/api/tasks/{id}/events`
- explicit validation error for invalid events limit
- `afterSeq` incremental fetch behavior
- structured terminal cancel error (`task_cancel_not_allowed`)

`smoke_full.sh` now includes Stage34 smoke.

## Residual risks
- Cancel remains cooperative (can only finalize after current task execution returns).
- Event storage remains embedded in `tasks.json` (not a dedicated append-only log stream).
- No CLI-level `agents run-events` surface yet; Stage34 focuses on HTTP runtime control path.
