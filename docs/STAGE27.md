# STAGE 27 — Source-driven parity slice: Browser `act` kinds expansion

## Goal
Increase `browser act` envelope fidelity to OpenClaw while preserving current NexaClaw CLI/API contracts and deterministic backend behavior.

## OpenClaw source references used
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/browser/routes/agent.act.shared.d.ts`
  - `ACT_KINDS`: `click, close, drag, evaluate, fill, hover, scrollIntoView, press, resize, select, type, wait`
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/browser/client-actions-core.d.ts`
  - `BrowserActRequest` shape and per-kind request fields
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/browser/pw-tools-core.interactions.d.ts`
  - interaction-level Playwright capability surface (hover/drag/select/press/type/fill/evaluate/scrollIntoView/wait)
- `openclaw browser --help`
  - concrete CLI command surface used for `openclaw_cli` backend dispatch (`hover`, `drag`, `select`, `fill`, `resize`, `evaluate`, `scrollintoview`, `press`, `wait`, `close`)

## Delivered
1) `openclaw_cli` backend: expanded `act` dispatch
- Added mapping for:
  - `press`, `hover`, `scrollIntoView`, `drag`, `select`, `fill`, `resize`, `evaluate`, `wait`, `close`
- `click`/`type` mapping retained.
- Added structured request validation errors for missing required fields (`key`, `fields`, `fn`, positive `width/height`).

2) Native backend: practical kind expansion + explicit capability gates
- Added native `act` support for:
  - `close` (target close lifecycle)
  - `hover` (ref validation + explicit success)
  - `scrollIntoView` (ref validation + explicit success)
  - `fill` (typed field application through existing native text-control model)
  - `resize` (persisted viewport update)
- Retained:
  - `click`, `type`, `press(Enter submit only)`, `wait`.
- Replaced silent no-op style behavior for unsupported press keys with structured capability error:
  - `code: native_capability_press_key_unsupported`
- Added explicit capability errors for unsupported act kinds in native runtime (`drag/select/evaluate`):
  - `code: native_capability_kind_unsupported`

3) Deterministic native state and snapshot improvements
- Native target state now persists viewport dimensions.
- Snapshot now includes `viewport.width/height`.

4) Smoke coverage
- Added `scripts/smoke_stage27_browser_act_kinds.sh` for new kind handling and error contracts.
- Integrated Stage27 smoke into `scripts/smoke_full.sh`.

## Compatibility and safety notes
- No silent unsupported-kind noop paths were added.
- Structured error contracts are explicit and machine-readable.
- Native backend remains intentionally constrained (no full JS runtime / CDP behavior); unsupported features report capability gates clearly.
