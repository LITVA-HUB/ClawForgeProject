# Changelog

All notable changes to NexaClaw are documented here.

## [0.2.0] — 2026-06

### Added

- **Task lane** (`/api/tasks`) with timeout/cancel and run-event introspection (`/api/tasks/{id}/events`); richer `cancelling` transition state (Stage 34)
- **Browser relay** native backend (`browser.backend=native`) for `status/open/navigate/snapshot/click/type/screenshot`; `openclaw_cli` fallback retained for compatibility (Stage 18/26/27/28)
- **Admin dashboard** (`/admin`) with KPI cards, refresh controls, session/cron visibility, safe cron quick-actions, event-log tail, audit tail (Stage 19)
- **Agent runtime enforcement** (Stage 33): live task-path enforcement for context carryover, history trimming, per-run tool allow/deny checks
- **Channels API**: `channels list|status|capabilities|resolve|add|remove` (Telegram baseline)
- **Message actions**: `message send|react|delete|poll --channel telegram` with strict target validation and `--dry-run`
- **Model auth profiles**: `models auth list|add|login|paste-token|setup-token|use|remove` + `models auth order get|set|clear`
- **Image routing**: `models set-image`, `image-fallbacks list|add|remove|clear`
- **Setup wizard**: guided `nexaclaw setup` terminal wizard with RU/EN UX
- **Multi-provider routing**: `openai`, `anthropic`, `openrouter`, `gemini`, `minimax` with `current` + `aliases` + `fallbacks`
- **Audit JSONL** trail for sensitive gateway actions
- **Per-source rate limiting** with sliding-window buckets
- **Scoped tools policy**: `global` / `channels` / `peers` policy levels
- **Session JSONL transcripts** with configurable `dmScope`
- **Cron semantics**: `sessionTarget`, `payload`, `delivery`, `wakeMode`; `run/runs/validate` sub-commands
- **RU/EN CLI UX**: `--lang ru|en --help`, `--doctor`, smoke and benchmark scripts

### Changed

- CMake target renamed to `nexaclaw`; `clawforge` alias binary retained for migration continuity
- Default browser backend changed from `stub` to `native` for new installs
- Auth token mode uses `Authorization: Bearer` header (standard)

### Fixed

- Rate limiter window now correctly evicts stale buckets on each `allow()` call
- Cron `runs` sub-command returns empty list gracefully when no runs exist
- Task lane returns `cancelling` state before final `cancelled` to avoid lost-update races

## [0.1.0] — initial

Initial private release. Core HTTP gateway, session store, basic Telegram polling, cron baseline, and OpenClaw-compatible CLI surface.
