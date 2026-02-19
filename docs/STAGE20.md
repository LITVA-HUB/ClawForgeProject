# STAGE20 — Native Browser Fidelity Uplift (slice 1)

## Goal
Move the built-in `browser.backend=native` flow one concrete step closer to Playwright/CDP runtime realism, while preserving the existing CLI/API contract and fallback compatibility.

## Delivered in slice 1

### 1) Native runtime content loading (practical uplift beyond deterministic-only emulation)
- Added a **runtime content resolver** for native backend targets:
  - `data:text/html,...` URLs are parsed directly (existing behavior retained).
  - `http(s)` URLs now attempt real HTML fetch using local `curl` when available.
- Native snapshots now derive refs/title from fetched HTML when runtime fetch succeeds.
- This provides materially better realism for common no-JS pages without changing command surface.

### 2) Capability detection + structured warnings/errors
- `browser status` for native backend now returns:
  - `nativeRuntime.httpFetch` capability flag
  - parsed URL schemes
  - structured warning support marker
- Native target operations (`open`, `navigate`, `snapshot`, link-driven `click`) return `runtime` metadata:
  - `runtime.source` (`data_url` / `http_fetch` / `url_only`)
  - optional `runtime.warning` with structured `code/message/...` when runtime fetch is unavailable or fails.
- Fallback is explicit and non-breaking (`url_only`) instead of opaque degradation.

### 3) CLI/API stability preserved
No endpoint or command shape breakage for:
- `status`
- `open`
- `navigate`
- `snapshot`
- `click`
- `type`
- `screenshot`

Enhancements are additive JSON fields only.

### 4) State persistence updated safely
Native persisted target state now stores runtime source/warning metadata alongside refs/typed values, using the existing atomic-ish tmp+rename write path.

### 5) Smoke coverage extension
- `scripts/smoke_stage18_native_browser.sh` now validates new runtime metadata behavior for native flow.
- Tests are capability-aware (`nativeRuntime.httpFetch`) to keep CI deterministic across environments where `curl`/network may differ.

## Known limits after slice 1
- Still not full browser engine parity (no JS execution loop, no real DOM event model, no CDP session control).
- Runtime fetch currently depends on `curl` and network reachability for `http(s)` pages.
- Dynamic app pages will still require `browser.backend=openclaw_cli` for full Playwright/CDP behavior.

## Next likely slice
- Introduce optional CDP-attached native mode (when local debuggable Chromium is present), with strict capability gating and structured `not_available` errors while preserving current deterministic+runtime fallback path.
