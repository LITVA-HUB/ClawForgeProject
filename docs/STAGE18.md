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

---

# Stage 18 — Slice 2 (Native Backend Fidelity + Reliability Hardening)

## Goal
Raise practical parity of native BrowserRelay beyond diagnostic baseline while keeping compatibility and preserving `openclaw_cli` fallback path.

## Implemented improvements

### 1) Native target lifecycle robustness
- Added persisted `activeTargetId` tracking to avoid lexicographic target selection issues (`native-10` vs `native-9`).
- Snapshot/screenshot now resolve empty `targetId` through active target semantics, returning structured `target_not_found` when unresolved.
- Native status now reports `activeTargetId` and current target count.

### 2) Ref stability + snapshot realism
- Added deterministic HTML extraction for core interactive tags in native mode:
  - `a`, `input`, `textarea`, `button` (+ synthetic document root)
- Added stable ref identity via element signatures persisted in state.
- Rebuild logic preserves existing refs where signatures match, reducing ref churn between snapshots.
- Snapshot now reflects typed values for text-like controls (`textbox/searchbox/combobox`), improving action->state parity.

### 3) Action side-effects realism
- `type` mutates target typed state and persists it, visible on subsequent snapshot.
- `click` on refs with known `href` now models navigation side-effect by updating target URL/title and rebuilding refs.
- Screenshot placeholder now includes resolved target and URL context.

### 4) State persistence/concurrency hardening
- Native state file switched from single global path to backend/profile-scoped hashed state key path in `/tmp`.
- Save path hardened with temp-file + rename atomic-style write (fallback to direct write on unsupported rename path).
- Persisted payload now includes `lastTargetId` and ref metadata needed for stable reconstruction.

### 5) Stage16 smoke reliability hardening (`openclaw_cli` path)
- Added bounded retry/backoff helper for browser smoke calls in `scripts/smoke_stage16_browser_oauth_message.sh`.
- Retries only on transient-like failures (timeouts/attachment/race-style symptoms), without masking deterministic hard failures.

## Smoke coverage updates
- Extended Stage18 smoke to assert practical parity behavior:
  - type -> snapshot text reflection
  - ref continuity across snapshots for the acted-upon control
  - click side-effect shape checks when navigation metadata is provided
- Stage16 smoke now includes resilient execution wrappers for flaky external CLI/browser timing.
