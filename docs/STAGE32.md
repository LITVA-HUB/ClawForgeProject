# STAGE 32 — Agents context/tooling deepening (slice 1)

## Goal
Deliver one high-impact, backward-compatible uplift in NexaClaw's `agent/agents` layer focused on context carryover/limits and per-agent tool policy metadata, aligned with OpenClaw subagent semantics.

## OpenClaw source refs inspected
- `/opt/homebrew/lib/node_modules/openclaw/docs/tools/subagents.md`
  - Context isolation and reduced system prompt for subagents
  - Tool policy defaults and allow/deny customization for subagents
  - `sessions_spawn` model/thinking/policy semantics
- `/opt/homebrew/lib/node_modules/openclaw/docs/cli/agent.md`
- `/opt/homebrew/lib/node_modules/openclaw/docs/cli/agents.md`
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/agent-scope.d.ts`
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/tool-policy.d.ts`

## What was implemented

### 1) New per-agent context policy metadata
`agents update` now supports additive context-policy fields:
- `--context-history-limit <n>` → stored as `agent.context.historyLimit` (normalized/clamped)
- `--context-carryover <inherit|minimal|none>` → stored as `agent.context.carryover`

Registry normalization now sanitizes persisted `context` fields during load/save.

### 2) New per-agent tool policy metadata
`agents update` now supports additive tool-policy fields:
- `--tool-allow <csv|*>` → stored as `agent.tools.allow`
- `--tool-deny <csv>` → stored as `agent.tools.deny`

Registry normalization sanitizes and deduplicates `tools.allow/deny` during load/save.

### 3) Run-time propagation (gateway path) + explicit fallback behavior
`agents run` now resolves context/tool policy from agent metadata and optional run overrides. These are embedded into:
- `run.options.context`
- `run.options.tools`
- gateway task payload (`/api/tasks`) as `context`/`tools`

If gateway is unavailable, these options are treated as advanced options and return structured:
- `error: "advanced_options_require_gateway"`
- with `unsupportedOptions` carrying context/tools payloads

This preserves prior local-session behavior and makes unsupported branches explicit.

### 4) Structured validation errors added
- `invalid_context_history_limit`
- `invalid_context_carryover`
- `invalid_tool_allow`
- `invalid_tool_deny`

All returned in existing JSON-first style.

## Smoke coverage
Added new smoke:
- `scripts/smoke_stage32_agents_context_tools.sh`

Checks:
- agent update with context/tool flags
- persisted metadata visible in `agents show`
- `agents run` with context/tool overrides yields structured `advanced_options_require_gateway` in local fallback
- invalid context carryover branch returns explicit structured error

`smoke_full.sh` updated to include Stage32 smoke.

## Compatibility notes
- Additive only; existing Stage21/29/31 agent flows remain intact.
- Existing registry rows without `context`/`tools` continue to work.
- Unknown agent subcommands still return structured `not_implemented` stubs.

## Residual gap
- Context/tool policy metadata is now first-class in CLI orchestration and gateway payloads, but full runtime enforcement parity (live per-agent tool filtering in embedded runner + context-pruning enforcement) is still pending.
