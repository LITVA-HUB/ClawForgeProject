# STAGE 29 — Source-driven parity slice: Agent orchestration metadata + richer run options

## Goal
Deepen `agent/agents` orchestration semantics beyond Stage 21 baseline by adding richer run controls and deterministic per-agent run history, while preserving additive compatibility and explicit structured errors for unsupported fallback branches.

## OpenClaw source references used
- CLI docs:
  - `/opt/homebrew/lib/node_modules/openclaw/docs/cli/agent.md`
  - `/opt/homebrew/lib/node_modules/openclaw/docs/cli/agents.md`
- Subagent/orchestration docs:
  - `/opt/homebrew/lib/node_modules/openclaw/docs/tools/subagents.md`
  - `/opt/homebrew/lib/node_modules/openclaw/docs/tools/index.md`
- Plugin SDK / source-derived type anchors:
  - `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/subagent-registry.d.ts`
    - `SubagentRunRecord` lifecycle metadata shape (`runId`, `createdAt/startedAt/endedAt`, `cleanup`, outcome)
  - `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/subagent-registry.store.d.ts`
    - persistence contract for run registry
  - `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/tools/agent-step.d.ts`
    - run-step envelope (`sessionKey`, `message`, `timeoutMs`, lane)

## Delivered
1) Additive `agents` orchestration command uplift
- Added `agents runs` (alias: `agents history`) with deterministic JSON output:
  - `agents runs [<id>|--agent <id>] [--limit <n>]`
- Preserves existing Stage 21 commands:
  - `list/show/create/delete/use/run`

2) Richer `agents run` options (additive)
- Extended `agents run` surface:
  - `--model <name>`
  - `--thinking <off|minimal|low|medium|high|xhigh>`
  - `--run-timeout-seconds <s>`
  - `--cleanup <keep|delete>`
  - existing `--timeout-ms` retained
- Options are passed through to `/api/tasks` request when gateway path is available.

3) Deterministic run metadata persistence
- Added JSONL run ledger:
  - `stateDir/agents/runs.jsonl`
- Each run appends a structured record including:
  - `runId`, `agentId`, `sessionKey`, `mode`, `status`, `message`, `startedAtMs`, `options`
- `agents runs/history` reads recent per-agent records with explicit `--limit`.

4) Clear lifecycle semantics + safe structured errors
- `agents run` now returns explicit run status metadata in output (`queued`/`submitted`/`stored`/`rejected`).
- If gateway is unavailable and advanced options are requested, command returns:
  - `{"ok":false,"error":"advanced_options_require_gateway",...}`
  - instead of silently degrading those options in local fallback mode.
- Invalid option values return explicit machine-readable errors:
  - `invalid_thinking`, `invalid_cleanup`, `invalid_run_timeout_seconds`, `invalid_timeout_ms`, `invalid_limit`.

## Compatibility and safety notes
- Existing Stage 21 behavior remains valid for baseline `agents run --message ...` path.
- Changes are additive and JSON-first.
- Unsupported/unsafe fallback branches are explicit (no hidden downgrades for advanced orchestration options).

## Tests
- Added `scripts/smoke_stage29_agents_orchestration.sh`:
  - baseline run still works
  - run history (`agents runs`, `agents history`) is populated
  - invalid `--thinking` returns structured error
  - advanced options without gateway return structured `advanced_options_require_gateway`
- Integrated into `scripts/smoke_full.sh`.
