# STAGE 26 — Source-driven parity slice: Browser `act` envelope

## Goal
Port a high-impact OpenClaw browser-control behavior into NexaClaw by introducing OpenClaw-style `act` request envelope support across API/CLI/gateway, with explicit structured errors and safe native fallback behavior.

## OpenClaw source references used
- Local installation docs:
  - `/opt/homebrew/lib/node_modules/openclaw/docs/cli/browser.md`
- Local compiled source map anchors:
  - `/opt/homebrew/lib/node_modules/openclaw/dist/routes-BZSffgT3.js`
    - `src/browser/routes/agent.act.shared.ts` (`ACT_KINDS`, click modifier/button parsing, selector guidance)
    - `src/browser/routes/agent.act.ts` (route shape, `kind` dispatch, request envelope semantics)
  - `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/tools/browser-tool.schema.d.ts`
    - Browser tool schema including `action:"act"` and `request.kind` payload surface

## Delivered
1) New API endpoint
- Added `POST /api/browser/act`
- Accepts either direct request body or nested `{"request": {...}}`
- Passes optional `targetId`
- Audits `browser_act` with `kind`

2) New BrowserRelay method
- Added `BrowserRelay::act(request, targetId)`
- Native backend (`browser.backend=native`) now supports:
  - `kind=click` → mapped to native click
  - `kind=type` → mapped to native type
  - `kind=press` → Enter form-submit modeling when form present; otherwise explicit noop-success
  - `kind=wait` → noop timing acknowledgment
- Unsupported native kinds return structured error:
  - `error: native_browser_act_kind_unsupported`
  - `supportedKinds: [click,type,press,wait]`

3) CLI + gateway parity
- Added CLI command:
  - `nexaclaw browser act --json '{...}' [--target-id <id>]`
- Added gateway call method:
  - `nexaclaw gateway call browser.act --params '{"request": {...}}'`

4) Structured compatibility behavior for `openclaw_cli` backend
- `act` maps to existing relay operations for supported kinds (`click`, `type`, `wait`)
- Unsupported kinds return explicit structured error:
  - `error: openclaw_cli_act_kind_unsupported_in_nexaclaw`

## Safety/compatibility notes
- JSON-first contract preserved
- Errors are explicit and machine-readable
- No destructive write operations added
- This is a pragmatic parity slice; not full Playwright/CDP execution parity yet

## Tests
- Added `scripts/smoke_stage26_browser_act.sh`
  - validates native `act` click/type/wait/press Enter behaviors
  - validates structured unsupported-kind error
  - validates gateway-call parity for `browser.act`
- Added Stage26 smoke into `scripts/smoke_full.sh`
