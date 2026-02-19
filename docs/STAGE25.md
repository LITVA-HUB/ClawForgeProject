# STAGE 25 — Slice 2: Nodes/Canvas/Devices practical runtime uplift

## Goal
Evolve Stage 22 registry-only baseline into a practical runtime control surface while keeping operations safe-by-default, JSON-first, and CLI/API compatible.

## Delivered

### 1) Nodes runtime uplift (read-safe)
- `nodes list|status|describe` now include additive runtime metadata for each node:
  - `runtime.available`
  - `runtime.mode` (`local-read-safe|unavailable`)
  - `runtime.allowedActions`
- `nodes status` now reports `runtimeReady` count.
- `nodes invoke` now performs meaningful read-safe runtime probe for local connected runtime (`local-node`) for actions:
  - `status`, `describe`, `probe`, `health`
- Probe payload includes host/user/platform/cwd/timestamp in structured JSON.

### 2) Devices practical runtime surface
- Added shared `devicesMethod()` with parity shape for:
  - `devices.list`
  - `devices.status`
  - `devices.invoke`
- `devices invoke` now supports safe actions (`status|probe|metrics`) returning structured runtime probe data.
- Non-allowed actions return explicit structured `device_action_not_allowed` errors with `allowedActions`.

### 3) Canvas practical runtime surface
- Added shared `canvasMethod()` with:
  - `canvas.status` runtime capability metadata
  - `canvas.snapshot` virtual safe snapshot payload (JSON-first)
  - `canvas.invoke` modeled safe actions (`present|hide|navigate|snapshot|status`)
- When runtime is unavailable, returns explicit structured errors:
  - `canvas_runtime_unavailable`
  - `readSafeOnly=true`

### 4) Gateway call parity extension
- `gateway call` now supports expanded control-plane methods:
  - `devices.list|status|invoke`
  - `canvas.status|snapshot|invoke`
- Existing `nodes.*` gateway call compatibility retained.

### 5) Compatibility + safety contracts preserved
- CLI families and aliases unchanged:
  - `nodes` and `node` alias
  - `devices`
  - `canvas`
- Responses remain JSON-first; new fields are additive.
- Invoke paths remain read-safe; writes/destructive actions are rejected with structured errors.

### 6) Smoke coverage uplift
- Added `scripts/smoke_stage25_nodes_runtime_slice2.sh`:
  - validates runtime metadata on nodes
  - validates read-safe runtime invokes for nodes/devices
  - validates canvas virtual snapshot and gateway parity
  - validates structured action-not-allowed canvas error
- Updated full smoke chain to include Stage25 smoke.
- Stage22 smoke kept compatible with both `baseline-stub` and `local-read-safe` canvas status modes.

## Notes / limits
- This is still not full multi-node remote orchestration or multi-channel device control.
- Runtime probes are intentionally local and read-safe.
- Canvas snapshot is virtual/runtime-modeled JSON payload (not full remote framebuffer streaming).
