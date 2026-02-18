# Stage 16 — Browser actions + OAuth login import + message action uplift

## Goal

Close three heavy practical gaps after Stage 15 baseline:

1. Browser command surface beyond `status/open/snapshot`
2. OAuth login UX baseline for `openai-codex`
3. Message actions beyond `send`

## Delivered

## 1) Browser parity uplift (practical baseline)

### Backend

`browser.backend` now supports:

- `stub`
- `shell`
- `openclaw_cli` (new)

New browser config fields:

- `browser.profile` (default `openclaw`)
- `browser.cliBinary` (default `openclaw`)

In `openclaw_cli` mode, NexaClaw delegates browser control to OpenClaw CLI (`openclaw browser --json ...`) and returns normalized JSON payloads.

### New BrowserRelay methods

Added:

- `navigate(url)`
- `click(ref, targetId, doubleClick)`
- `type(ref, text, targetId, submit, slowly)`
- `screenshot(targetId, fullPage, type)`

`snapshot()` now uses AI snapshot format (`--format ai`) for click/type-compatible refs.

### HTTP endpoints

Added:

- `POST /api/browser/navigate`
- `POST /api/browser/click`
- `POST /api/browser/type`
- `POST /api/browser/screenshot`

Existing endpoints kept:

- `GET /api/browser/status`
- `POST /api/browser/open`
- `POST /api/browser/snapshot`

### CLI

Added/expanded:

- `nexaclaw browser navigate <url>`
- `nexaclaw browser click <ref> [--target-id <id>] [--double]`
- `nexaclaw browser type <ref> <text> [--target-id <id>] [--submit] [--slowly]`
- `nexaclaw browser screenshot [targetId] [--full-page] [--type png|jpeg]`

## 2) OAuth flow baseline uplift (`models auth login`)

Added:

- `models auth login --provider openai-codex`
  - runs `openclaw models auth login --provider openai-codex` (interactive OAuth)
  - imports token from OpenClaw auth store into NexaClaw auth profiles

Import helpers:

- `--openclaw-auth-file <path>` for explicit auth-store path
- `--skip-login` to import-only mode (useful for automation/smokes)
- `--profile-id <dest-id>` to set destination profile id in NexaClaw
- `--source-profile-id <id>` to pick a specific imported OpenClaw profile

Added auth profile ordering controls:

- `models auth order get --provider <name>`
- `models auth order set --provider <name> --profile-id <id> [--profile-id <id2> ...]`
- `models auth order clear --provider <name>`

Auth resolver now checks provider candidates in this priority order:

1. active profile
2. configured order list
3. remaining provider profiles
4. env fallback (`apiKeyEnv`)

First non-expired profile token wins.

## 3) Message actions uplift (Telegram baseline)

Added actions:

- `message react`
- `message delete`
- `message poll`

All support Telegram target validation and `--dry-run` payload preview.

`channels capabilities` for telegram updated to reflect baseline support:

- `react: true`
- `delete: true`
- `poll: true`

## Smoke coverage

Added:

- `scripts/smoke_stage16_browser_oauth_message.sh`

Coverage includes:

- browser `openclaw_cli` happy path (`status/open/snapshot/click/navigate/type/screenshot`)
- browser error path (invalid `cliBinary`)
- OAuth import flow (`models auth login` import-only + auth order set/get/clear)
- message actions dry-run (`react/delete/poll`)

Integrated into:

- `scripts/smoke_full.sh`

## Known limits (explicit)

- Browser backend is practical parity via OpenClaw CLI delegation, not pure native CDP/Playwright inside NexaClaw.
- OAuth login path depends on external `openclaw` binary and auth-store format.
- Message action uplift remains Telegram-only; multi-provider parity is still roadmap.
