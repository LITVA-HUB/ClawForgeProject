# STAGE19 — Browser Admin Dashboard (slice 1 + slice 2)

## Goal
Ship a built-in browser admin console (`/admin`) that is actually useful for day-to-day operator work: health visibility, session/cron insight, recent logs/audit, and safe quick actions.

## Slice 1 (baseline, already delivered)

### 1) Admin UI route
- Added `GET /admin` returning embedded HTML/JS dashboard.
- Initial sections:
  - health/status overview
  - sessions overview
  - cron jobs overview
  - recent logs tail (EventBus events)
  - audit tail (JSONL file)
- Quick control actions (non-destructive):
  - run now
  - enable
  - disable

### 2) API reuse + minimal additions
Reused existing endpoints:
- `/api/status`
- `/api/sessions`
- `/api/cron/jobs`
- `/api/cron/jobs/{id}/run|enable|disable`

Added admin-focused read endpoints:
- `GET /api/admin/overview`
- `GET /api/admin/logs/tail?limit=N`
- `GET /api/admin/audit/tail?limit=N`

### 3) Security posture
- Existing `/api/*` auth + rate-limit middleware stays active.
- Dashboard supports optional Bearer token in browser local storage.
- No destructive cron delete/remove in UI.

### 4) Smoke coverage
- `scripts/smoke_stage19_admin_dashboard.sh` validates `/admin`, admin endpoints, and cron action wiring.

---

## Slice 2 (polish/deepening)

### 1) `/admin` upgraded from baseline page to operator console
- Reworked UI layout for higher information density:
  - top health strip + connection state
  - KPI cards (service/uptime/sessions/cron health)
  - sessions table sorted by recency
  - cron quick-control area with visible run/error fields
  - logs/audit panes + raw overview pane for drilldown
- Added practical controls:
  - auto-refresh interval selector (`off/3s/5s/10s/30s`)
  - pause/resume auto-refresh
  - explicit manual refresh
  - token save/reuse in local storage

### 2) Observability details improved
- UI now exposes important operational fields directly:
  - sessions: key/sessionId/updatedAt/age
  - cron: enabled state, next run, last run, consecutive error count
  - overview: recent events and rich status blocks
- Cron action defaults are safer by default:
  - quick “Run” uses `mode: "due"` (non-force path)

### 3) Minimal API enhancement (reuse-first)
- Kept existing endpoint surface and reused existing `/api/sessions`, `/api/cron/jobs`, `/api/admin/logs/tail`, `/api/admin/audit/tail`.
- Enriched `GET /api/admin/overview` with:
  - recent sessions sample
  - cron summary (`enabled`, `jobsWithErrors`) + job sample
  - recent event sample
- Errors remain strict JSON objects with `ok/error` conventions used across API.

### 4) Security posture (unchanged + reinforced)
- No new unsafe/destructive UI controls exposed.
- `/api/*` auth middleware is still the single gatekeeper.
- Dashboard remains local/loopback operational console by design.

### 5) Smoke coverage extended
- `scripts/smoke_stage19_admin_dashboard.sh` now verifies:
  - upgraded `/admin` sections are present
  - `/api/admin/overview` returns enriched fields used by UI
  - `/api/sessions` + `/api/cron/jobs` wiring required by UI
  - admin logs/audit tails and safe cron run path

## Notes / known limits
- Embedded static dashboard (no SPA framework), intentionally dependency-light.
- Log tail is EventBus-derived; not a full process log index.
- No pagination/search yet for large session/job sets.
- No RBAC model yet; relies on existing API auth mode and deployment perimeter.
