# STAGE 31 — Advanced agent orchestration slice: metadata controls + run lifecycle introspection

## Goal
Push agent/orchestration parity beyond Stage29 with a source-driven, additive uplift focused on richer agent config semantics and stronger run lifecycle/status handling, while keeping JSON-first outputs and explicit structured errors.

## OpenClaw source references used
- CLI docs:
  - `/opt/homebrew/lib/node_modules/openclaw/docs/cli/agent.md`
  - `/opt/homebrew/lib/node_modules/openclaw/docs/cli/agents.md`
- Subagent docs:
  - `/opt/homebrew/lib/node_modules/openclaw/docs/tools/subagents.md`
- SDK type references:
  - `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/subagent-registry.d.ts`
  - `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/subagent-registry.store.d.ts`
  - `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/tools/agent-step.d.ts`

## What was implemented

### 1) Richer `agents` metadata/profile controls
Added new additive command:
- `agents update <id>|--agent <id> [--name ...] [--session-key ...] [--profile ...] [--description ...] [--role ...] [--tags <csv>] [--subagent-model ...] [--subagent-thinking ...] [--allow-agents <csv|*>] [--max-concurrent <n>] [--archive-after-minutes <n>]`

This writes richer orchestration metadata into the existing agent registry (`stateDir/agents/agents.json`) while preserving compatibility with older rows.

### 2) Run lifecycle metadata + richer status handling
`agents run` now persists and returns richer lifecycle fields (additive):
- `state` (`accepted|finished|failed`)
- `terminal` (`true|false`)
- `createdAtMs`, `startedAtMs`, optional `endedAtMs`

Run records remain deterministic and JSONL-backed at:
- `stateDir/agents/runs.jsonl`

### 3) New run introspection + filtering
Added:
- `agents run-status <run-id>|--run-id <id> [--agent <id>]`
- `agents runs ... [--status <queued|submitted|running|completed|failed|stored|rejected>] [--active]`

This allows deterministic per-run inspection and queue-like active filtering without changing existing command shapes.

### 4) Structured errors retained/extended
Added explicit structured errors for new unsupported/invalid branches:
- `invalid_subagent_thinking`
- `invalid_status`
- `invalid_max_concurrent`
- `invalid_archive_after_minutes`
- `run_not_found`
- existing `advanced_options_require_gateway` behavior preserved

## Smoke coverage
Added:
- `scripts/smoke_stage31_agents_advanced.sh`

Coverage validates:
- agent metadata update path
- run lifecycle presence (`runId`, status/state compatibility)
- `run-status` retrieval
- runs filtering path
- structured validation error on invalid subagent thinking

`smoke_full.sh` updated to include Stage31 smoke.

## Compatibility notes
- All changes are additive.
- Existing Stage21/29 flows (`list/show/create/delete/use/run/runs/history`) continue to work.
- Unknown subcommands still return structured `not_implemented` stubs.

## Remaining gap (explicit)
This slice improves orchestration metadata + run lifecycle observability but does **not** implement true external runtime subagent controls (stop/log/info/send live control plane parity).
