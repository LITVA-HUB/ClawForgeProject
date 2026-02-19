# STAGE 30 — Source-driven parity slice: native browser `act.wait` fidelity uplift

## Goal
Deepen native browser runtime parity on one high-impact behavior path by replacing `act.wait` no-op semantics with real runtime waiting behavior, while keeping API/CLI compatibility stable and explicit capability gates for unsupported wait contracts.

## OpenClaw source references used
- OpenClaw browser CLI docs:
  - `/opt/homebrew/lib/node_modules/openclaw/docs/cli/browser.md`
- OpenClaw browser tool docs:
  - `/opt/homebrew/lib/node_modules/openclaw/docs/tools/browser.md`
- OpenClaw runtime implementation signal (dist):
  - `/opt/homebrew/lib/node_modules/openclaw/dist/pw-ai-CQUIpzrm.js`
    - `waitForViaPlaywright(opts)` lifecycle ordering (`timeMs` then `text`/`textGone`/selector/url/loadState/fn)

## Delivered
1) Native `act.wait` real behavior (not cosmetic)
- Replaced documented no-op with real waiting lifecycle in native backend:
  - `timeMs` sleep support
  - `text` wait-until-present in native target corpus
  - `textGone` wait-until-absent in native target corpus
  - `timeoutMs` bounded polling
- Target resolution now follows active-target semantics for wait checks (uses explicit target or active target).

2) Structured capability gates for unsupported wait paths
- Added explicit structured native capability errors for wait contracts not yet modelled in native runtime:
  - `selector` → `native_capability_wait_selector_unsupported`
  - `url` → `native_capability_wait_url_unsupported`
  - `loadState` → `native_capability_wait_load_state_unsupported`
  - `fn` → `native_capability_wait_fn_unsupported`
- Added explicit timeout errors for text waits:
  - `native_wait_text_timeout`
  - `native_wait_text_gone_timeout`

3) Compatibility retained
- Stable command/API paths preserved:
  - `browser status/open/navigate/snapshot/click/type/screenshot/act`
- No contract break for existing act envelope or openclaw_cli branch.

4) Smoke coverage uplift
- Added `scripts/smoke_stage30_browser_wait_native.sh`:
  - successful `wait text`
  - successful `wait textGone` when absent
  - timeout error check for `textGone` when still present
  - capability-gate error for unsupported `selector`
- Integrated into `scripts/smoke_full.sh`.

## Changed files
- `src/browser/BrowserRelay.cpp`
- `scripts/smoke_stage30_browser_wait_native.sh`
- `scripts/smoke_full.sh`
- `docs/STAGE30.md`
- `docs/CLI_PARITY.md`
- `docs/PARITY_ROADMAP.md`
- `README.md`

## Notes / residual limitations
- Native wait corpus is model-based (title/url/parsed text/typed values), not full DOM visibility or render-state semantics.
- Playwright-level waits (`selector`/`url` glob/loadState/fn`) remain intentionally capability-gated in native backend; use `browser.backend=openclaw_cli` for full behavior.
