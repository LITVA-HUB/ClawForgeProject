# STAGE 28 — Source-driven parity slice: Native browser runtime realism (`act.evaluate`)

## Goal
Deepen native browser runtime behavior toward more real interactive execution by implementing a constrained but practical `act.evaluate` flow, while preserving CLI/API compatibility and strict capability-gated error contracts.

## OpenClaw source references used
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/browser/routes/agent.act.shared.d.ts`
  - canonical `ACT_KINDS` includes `evaluate`
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/browser/client-actions-core.d.ts`
  - `BrowserActRequest` / `BrowserActResponse` shape (`kind:"evaluate"`, `fn`, optional `ref`, `result`)
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/browser/pw-tools-core.interactions.d.ts`
  - `evaluateViaPlaywright` and runtime-oriented interaction semantics
- `docs/STAGE26.md`, `docs/STAGE27.md`
  - previous NexaClaw baseline/contracts retained

## Delivered
1) Native `act.evaluate` implemented (runtime realism uplift)
- Added constrained JS runtime model for native backend evaluate:
  - available model objects: `location.href`, `document.title`, optional `element` when `ref` is provided
  - sync evaluate returns `result` in API-compatible shape
- Uses a minimal Node `vm` execution lane to avoid broad engine embedding while keeping integration low-risk.

2) Dynamic state mutation bridging from evaluate back to native browser target
- If evaluate mutates `location.href`, native target performs modeled navigation/runtime refresh.
- If evaluate mutates `document.title`, title is persisted.
- If evaluate mutates `element.value` for a known input `ref`, typed state is persisted and reflected in subsequent snapshots.
- This turns evaluate from deterministic no-op/unsupported behavior into observable runtime state transitions.

3) Strict structured capability errors (no silent fallback)
- Missing `fn` -> `browser_act_evaluate_fn_required`
- Missing node runtime -> `native_capability_evaluate_runtime_unavailable`
- Async evaluate unsupported -> `native_capability_evaluate_async_unsupported` with `capabilityGate`
- Invalid target/ref and runtime failures remain structured (`target_not_found`, `ref_not_found`, `native_evaluate_*` codes)

4) Compatibility preserved
- Existing command/API contracts kept stable for:
  - `browser status/open/navigate/snapshot/click/type/screenshot/act`
- OpenClaw CLI backend dispatch unchanged for real Playwright-style execution path.

5) Smoke coverage updates
- Extended `scripts/smoke_stage27_browser_act_kinds.sh`:
  - validate successful native `act.evaluate` result path
  - validate evaluate-driven navigation mutation (`location.href`)
  - validate async evaluate strict capability-gated failure

## Notes / limitations
- Native evaluate is intentionally constrained and sync-only.
- Full DOM/JS/CDP parity still belongs to `browser.backend=openclaw_cli`.
- This slice focuses on runtime realism uplift with explicit boundaries, not hidden emulation.
