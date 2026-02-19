# STAGE 22 — Slice 1: Nodes / Canvas / Devices baseline

## Goal
Seed practical CLI/API parity for the nodes/canvas/devices ecosystem in a **safe local baseline** mode, without multi-channel runtime scope.

## Delivered

### 1) New CLI baseline families
- `nodes` and `node` alias:
  - `list`
  - `status`
  - `describe [id|--node <id>]`
  - `invoke [action|--action <name>] [--node <id>]`
- `devices`:
  - `list`
  - `status`
  - `invoke --device <id> --action <name>`
- `canvas`:
  - `status`
  - `list`
  - `snapshot` (structured unavailable stub)
  - `invoke` (structured unavailable stub)

All outputs are JSON-first and deterministic for automation.

### 2) Safe local baseline behavior
- Nodes registry source: `stateDir/nodes/registry.json` when available.
- Fallback default registry: a single `local-node` baseline entry.
- `invoke` paths are read-safe only in this slice.
- Non-read-safe or unavailable runtime actions return explicit structured errors (`invoke_not_available_in_baseline`, `canvas_runtime_unavailable`).

### 3) Gateway API parity seed (`gateway call`)
Added methods:
- `nodes.list`
- `nodes.status`
- `nodes.describe`
- `nodes.invoke`
- `devices.list`
- `canvas.status`
- `canvas.invoke`

This provides immediate CLI/API parity seed with explicit boundaries.

### 4) Compatibility evolution
`nodes` / `node` / `devices` moved from top-level compatibility stubs to baseline implementations.

### 5) Smoke coverage
- New smoke: `scripts/smoke_stage22_nodes_canvas_devices.sh`
- Added to `scripts/smoke_full.sh`

Coverage includes:
- nodes list/status/describe
- devices list
- canvas status
- gateway call methods (`nodes.list`, `canvas.status`)
- guarded invoke failure path

## Notes and limits
- No external paired-node runtime yet.
- No real canvas rendering pipeline yet.
- No write/destructive device actions in this slice by design.
- Multi-channel scope intentionally excluded.

## Next slice ideas
- Persist/manage node registry via explicit commands.
- Add richer read operations (`camera_list`, `screen_record` metadata-only stubs, `location_get` capability status).
- Introduce guarded real runtime adapters behind capability checks.
