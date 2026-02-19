# STAGE21 — `agent/agents` CLI baseline + practical orchestration uplift

## Goal
Close the highest-priority OpenClaw CLI parity gap after Stage20 by introducing a practical `agent`/`agents` command-family baseline that works with current NexaClaw architecture (single gateway + session/task infrastructure), while keeping behavior deterministic and safely scoped.

## Delivered

### 1) New `agent` / `agents` command-family baseline
Implemented in CLI dispatcher with JSON-first UX:

- `agents list`
- `agents show <id>`
- `agents create <id> [--name ...] [--session-key ...]`
- `agents delete <id>` (`main` is protected)
- `agents use <id>`
- `agents run [<id>|--agent <id>] --message <text> [--timeout-ms <ms>]`
- `agent ...` alias family (same baseline)

Behavior is deterministic and file-backed via `stateDir/agents/agents.json` with normalization on load:
- guaranteed `main` agent,
- active-agent pointer,
- strict id validation,
- duplicate/invalid entry cleanup.

### 2) Practical orchestration reuse (task/session stack)
`agents run` uses existing infra in safe fallback layers:

1. try gateway task lane (`POST /api/tasks`) for async orchestration;
2. fallback to direct gateway message (`POST /api/message`) with target sessionKey;
3. fallback to local `SessionStore` append (user message only) when gateway unavailable.

This reuses existing queue/session design instead of introducing a parallel orchestration subsystem.

### 3) Structured stubs for unavailable subpaths
Unknown `agent/agents` subcommands now return explicit structured JSON compatibility stubs, e.g.:
- `{"ok": false, "error": "not_implemented", "command": "agents", ...}`

This preserves parity UX style used in other compatibility branches.

### 4) Smoke coverage
Added:
- `scripts/smoke_stage21_agents.sh`

Covers baseline list/create/use/show/run/delete + structured stub check.
Integrated into:
- `scripts/smoke_full.sh`

## Notes / limits
- This slice does **not** add multi-channel or external subagent runtime parity.
- `agents run` local fallback stores user turn deterministically but does not force offline model execution.
- Advanced OpenClaw-specific subpaths remain stubs by design in this stage.
