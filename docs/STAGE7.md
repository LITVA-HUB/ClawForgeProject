# Stage 7 — Orchestration + hardening baseline

## Done

- Internal async task lane (`TaskQueue`):
  - persistent queue/state in `state/tasks/tasks.json`
  - restart-safe recovery: previously `running` tasks are marked cancelled with reason
  - safe cancel flow for queued/running tasks
  - timeout status (`timeout`) if execution exceeds task timeout
- API endpoints:
  - `POST /api/tasks` — enqueue long run
  - `GET /api/tasks` — list
  - `GET /api/tasks/<id>` — status/details
  - `POST /api/tasks/<id>/cancel` — cancel
- Reliability/security:
  - rate limit per source (`rateLimit` config)
  - structured audit trail JSONL (`audit.file`)

## Config

- `taskLane.enabled|maxQueue|defaultTimeoutMs`
- `rateLimit.enabled|maxRequests|windowMs`
- `audit.enabled|file`

## Known limits

- Single worker thread (practical baseline, not multi-queue scheduler).
- Cancel for running task is cooperative/stateful (does not hard-kill external process).
