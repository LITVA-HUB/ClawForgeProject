# STAGE 17 — Slice 2: Native OAuth Device-Code START + Poll (`openai-codex`)

## Scope

This slice extends the Stage 17 native OAuth baseline under:

- `models auth login --provider openai-codex`

Goal: native NexaClaw can now start (acquire) device codes directly from OpenAI-compatible endpoint, while preserving existing poll/store behavior and OpenClaw bridge fallback.

## What changed

### 1) Native START acquisition in CLI (`models auth login`)

Native mode now supports two input paths:

- **Provided payload** via `--device-code-json <json|@file>` (existing behavior)
- **Auto-start acquisition** when `--device-code-json` is omitted

New practical flags/envs for start orchestration:

- `--device-start-url <url>` or env `OPENAI_CODEX_DEVICE_START_URL`
  - default: `https://api.openai.com/v1/oauth/device/code`
- `--client-id <id>` or env `OPENAI_CODEX_CLIENT_ID`
- `--scope <scope>` or env `OPENAI_CODEX_SCOPE`

Structured JSON start output remains machine-friendly and includes:

- `phase: "device_code_ready"`
- `deviceCode.device_code`
- `deviceCode.user_code`
- `deviceCode.verification_uri`
- `deviceCode.verification_uri_complete`
- `deviceCode.interval`
- `deviceCode.expires_in`

Start-phase failures now return structured JSON with `phase: "device_code_start"`, error metadata, URL, and HTTP status where available.

### 2) Poll/store behavior unchanged (interoperable with start)

- `--poll` still exchanges `device_code` at token endpoint (`--token-url`)
- Success still stores OAuth access token to NexaClaw auth profile store and sets provider active profile
- Retry semantics unchanged:
  - `authorization_pending` / `slow_down` => `retryable: true`, exit code `2`
  - terminal errors => exit code `1`

Interoperability added: one command can now do native start + poll when `--poll` is used without `--device-code-json`.

### 3) Bridge fallback preserved

Legacy OpenClaw import bridge remains available via:

- `--bridge-import`
- `--openclaw-auth-file`
- `--skip-login`
- `--source-profile-id`

Bridge output still includes `"bridge": true`.

## Smoke coverage

Updated `scripts/smoke_stage17_native_oauth.sh` now validates:

1. native start success without `--device-code-json`
2. native start structured error path
3. poll retryable error path (`authorization_pending`)
4. start→poll interoperability in one native command
5. bridge fallback import compatibility
