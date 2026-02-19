# STAGE 24 — Slice 2: Native browser runtime fidelity uplift

## Goal
Deepen native browser backend behavior from static ref emulation toward practical runtime fidelity while keeping CLI/API surface stable (`status/open/navigate/snapshot/click/type/screenshot`).

## Delivered

### 1) Native form-runtime modeling (meaningful fidelity step)
- Native HTML parser now tracks basic `<form>` context and binds controls to forms.
- `browser type <ref> <text> --submit` on native backend now performs a modeled submit when ref belongs to a form:
  - supports `method=get`,
  - resolves action URL (absolute/relative),
  - encodes fields into query params,
  - updates target URL and runtime/snapshot state via normal navigation flow.
- `browser click <ref>` now triggers modeled submit when clicking submit controls (`<button type=submit>` and submit-like inputs) linked to forms.

### 2) Capability gating + structured errors/warnings
- Native backend advertises capability gates via `browser status`:
  - `nativeRuntime.capabilityGates.formSubmit=true`
  - `formSubmitMethods=["get"]`
  - explicit unsupported list.
- Unsupported form submit methods return structured error:
  - `code=native_capability_form_method_unsupported`
  - `capabilityGate` object with feature/supported/requested metadata.
- Non-text `browser type` attempts now return structured error:
  - `code=native_type_ref_not_text_input`
- Submit-without-form-context stays non-fatal with structured warning:
  - `code=native_form_submit_no_form_context`

### 3) Compatibility preserved
- No CLI/API command names changed.
- Existing response keys remain compatible; new runtime metadata is additive.
- `browser.backend=openclaw_cli` path remains unchanged for real full automation.

### 4) Smoke coverage uplift
- Extended `scripts/smoke_stage18_native_browser.sh` to validate:
  - GET form submit modeled navigation with encoded query,
  - structured capability error for unsupported POST form submit.
- Existing Stage18 checks (runtime metadata, refs/type/screenshot, structured target errors) remain intact.

## Notes / limits
- Native backend still does not execute JS, maintain full DOM/event loop semantics, or provide CDP session fidelity.
- Form modeling is intentionally scoped to practical baseline behavior (GET submit path) with explicit capability gates for unsupported methods.
