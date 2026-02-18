# STAGE 17 — Slice 1: Native OAuth Device-Code Baseline (`openai-codex`)

## Scope

This slice introduces a native NexaClaw baseline for OAuth device-code login under:

- `models auth login --provider openai-codex`

Goal: remove hard dependency on external `openclaw models auth login` for the practical baseline path, while keeping bridge import compatibility.

## What changed

### 1) Native CLI path (`models auth login`)

New baseline flags:

- `--device-code-json <json|@file>` — pass device-code payload inline or via file (`@path`)
- `--poll` — poll token endpoint and store resulting access token in NexaClaw auth store
- `--client-id <id>` — OAuth client id for poll (or env `OPENAI_CODEX_CLIENT_ID`)
- `--token-url <url>` — token endpoint override (default: `https://api.openai.com/v1/oauth/token`)

Behavior:

- Without bridge flags, login defaults to **native mode**.
- Native mode validates payload and prints structured JSON errors.
- Without `--poll`, command returns a JSON phase payload (`device_code_ready`) for non-interactive orchestration.
- With `--poll`, command exchanges `device_code` for `access_token`; on success stores profile as `oauth_token` and sets provider active profile.

Retry/error semantics in poll mode:

- `authorization_pending` / `slow_down` -> JSON `retryable: true` and non-zero exit (2)
- terminal OAuth errors -> JSON `retryable: false` and non-zero exit (1)
- malformed/non-JSON transport responses -> explicit JSON diagnostics

### 2) Backward-compatible bridge retained

Legacy import flow stays available and can be forced with:

- `--bridge-import`

Legacy flags also auto-select bridge behavior:

- `--openclaw-auth-file`
- `--skip-login`
- `--source-profile-id`

Bridge result now includes `"bridge": true` in output JSON.

## Notes

- This slice focuses on native poll/store baseline and robust UX/error JSON.
- Direct native device-code *issuance/start* endpoint orchestration is still a follow-up item.
