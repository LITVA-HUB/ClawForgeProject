# Stage 6 — Scoped policy + Browser relay baseline

## Done

- Scoped tools policy schema in config:
  - `toolsPolicy.scopes.global`
  - `toolsPolicy.scopes.channels[<channel>]`
  - `toolsPolicy.scopes.peers["<channel>:<peerId>"]`
- Backward compatibility with legacy `toolsPolicy.allow/deny`.
- Policy checks wired into tool calls from:
  - API tools endpoint (`/api/tools/<tool>`)
  - Inbound sessions (`/api/inbound` + Telegram via session context)
  - Agent `/tool` command.
- Browser relay baseline endpoints:
  - `GET /api/browser/status`
  - `POST /api/browser/open`
  - `POST /api/browser/snapshot`
- Snapshot is an honest diagnostic stub (returns `implemented=false`), not fake data.
- Smoke: `scripts/smoke_stage6.sh`.

## Notes

- `browser.backend=shell` uses `browser.openCommand` (default `open` on macOS).
- `browser.backend=stub` does not spawn browser process.

## TODO (next stages)

- Real browser snapshot capture backend (Playwright/CDP).
- Browser tab/session correlation with API task lane.
