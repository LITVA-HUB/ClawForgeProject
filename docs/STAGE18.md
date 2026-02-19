# Stage 18 — Slice 1 (Native Browser Backend Baseline)

## Goal
Start native browser backend support in NexaClaw runtime for core browser flow (`status/open/navigate/snapshot/click/type/screenshot`) without requiring external `openclaw` CLI.

## What was implemented

### 1) Backend selection extension
- `browser.backend` now recognizes `native`.
- Existing `openclaw_cli` backend remains intact for fallback/compatibility.

### 2) Native backend baseline in `BrowserRelay`
Implemented a practical native diagnostic adapter with structured error responses:
- `status()`:
  - reports `backend: native`, capability list, diagnostic mode and limitations
- `open(url)`:
  - creates internal `targetId` (`native-<n>`) and in-memory target state
- `navigate(url, targetId)`:
  - updates existing target, or creates target when `targetId` omitted
- `snapshot(urlHint, targetId)`:
  - returns AI-style shape with `targetId`, `format: ai`, and `refs`
  - supports `urlHint` pre-navigation and `targetId` parity
- `click(ref, targetId)` / `type(ref,text,targetId,...)`:
  - validates `targetId` + `ref`
  - returns structured errors (`code: target_not_found|ref_not_found`)
- `screenshot(targetId,...)`:
  - writes deterministic placeholder screenshot artifact to `/tmp` and returns `path`

### 3) Command/API parity wiring
No new endpoint surface was required; existing CLI and HTTP API already pass through:
- `targetId` handling is preserved on all core browser actions.
- `ref` handling is preserved in `click/type`.

### 4) Smoke coverage
Added `scripts/smoke_stage18_native_browser.sh`:
- happy path:
  - native `status/open/snapshot/click/type/screenshot`
- error path:
  - invalid `targetId` for snapshot/click returns `code: target_not_found`

Integrated into `scripts/smoke_full.sh`.

## Constraints / Known limitations
- Native backend is currently **diagnostic baseline**, not full CDP/Playwright automation.
- Snapshot refs are deterministic emulation from local target state / simple data URL HTML hints.
- For real browser automation parity, continue using `browser.backend=openclaw_cli`.

## Compatibility
- `openclaw_cli` backend behavior is unchanged.
- Stage16 smoke remains valid.
