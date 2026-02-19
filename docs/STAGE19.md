# STAGE19 — Browser Admin Dashboard Baseline (slice 1)

## Goal
Ship a lightweight built-in web admin dashboard (served by NexaClaw HTTP server) so operator can monitor and control core runtime from browser, with Telegram-only communication still sufficient.

## Delivered (slice 1)

### 1) Admin UI route
- Added `GET /admin` returning embedded HTML/JS dashboard.
- Dashboard sections:
  - health/status overview
  - sessions overview
  - cron jobs overview
  - recent logs tail (EventBus events)
  - audit tail (JSONL file)
- Quick control actions include explicit buttons for **non-destructive** cron ops:
  - run now
  - enable
  - disable

### 2) API reuse + minimal additions
Reused existing endpoints:
- `/api/status`
- `/api/sessions`
- `/api/cron/jobs`
- `/api/cron/jobs/{id}/run|enable|disable`

Added missing admin-focused read endpoints:
- `GET /api/admin/overview`
- `GET /api/admin/logs/tail?limit=N`
- `GET /api/admin/audit/tail?limit=N`

All return JSON with `ok` and bounded `limit` behavior.

### 3) Security posture
- Dashboard designed for local/loopback deployment assumptions.
- Existing `/api/*` auth/rate-limit guard remains in force; dashboard supports optional Bearer token in browser local storage.
- No delete/remove job action exposed in UI.

### 4) Smoke coverage
- Added `scripts/smoke_stage19_admin_dashboard.sh`:
  - validates `/admin` is served
  - validates new admin endpoints
  - validates cron run wiring from admin-related flow
- Integrated into `scripts/smoke_full.sh`.

## Notes / known limits
- Dashboard is intentionally lightweight (no SPA framework, embedded page).
- `logs/tail` currently reflects recent EventBus entries, not arbitrary external process log files.
- Future slices can add pagination, richer filtering, auth UX hardening, and role-based action policies.
