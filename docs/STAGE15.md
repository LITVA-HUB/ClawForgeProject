# Stage 15 — Message + Channels baseline (strict telegram send)

## Goal

Close the next heavy parity gap after cron semantics: practical outbound messaging and channel management baseline.

## Delivered

## `message` command baseline

Implemented:

- `nexaclaw message send --channel telegram --target <...> --message <...>`
- `--dry-run` mode for safe validation/testing

Validation:

- strict target parsing for Telegram:
  - `@username`
  - numeric chat id (`-100...` / `123...`)
  - topic target (`chatId:topic:threadId`)
- rejects ambiguous/invalid targets with explicit error
- requires `--target` and (`--message` or `--media`)

Current limits:

- baseline supports `telegram` channel only
- `--media` is currently explicit `not implemented yet`
- other message actions (`react/delete/poll/...`) remain roadmap

## `channels` command baseline

Implemented:

- `nexaclaw channels list`
- `nexaclaw channels status`
- `nexaclaw channels capabilities`
- `nexaclaw channels resolve --channel telegram <target>`
- `nexaclaw channels add --channel telegram [--token-env ENV] [--dm-policy ...]`
- `nexaclaw channels remove --channel telegram`

Behavior:

- edits config in-place for add/remove baseline (`telegram.enabled`, optional env/policy)
- reports token env and token presence diagnostics

## CLI/help updates

- Help text expanded with `message send` and `channels ...` sections.
- `message`/`channels` removed from pure top-level compatibility stubs and now have real handlers.

## Smoke coverage

Added:

- `scripts/smoke_stage15_message_channels.sh`
- included in `scripts/smoke_full.sh`

Checks:

- channel list/add/status/remove path
- message send dry-run (explicit channel and auto-selected channel)
- strict invalid target rejection

## Notes

This is a practical baseline focused on operational utility and strict target safety. Full OpenClaw parity for multi-channel actions/providers still requires additional channel backends and action surfaces.
