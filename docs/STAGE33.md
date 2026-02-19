# STAGE 33 — Agents policy enforcement depth (slice 2)

## Goal
Close the Stage32 residual gap by enforcing agent run context/tool policies in the runtime path (not only storing/forwarding metadata), while preserving explicit JSON errors and backward compatibility.

## OpenClaw source refs inspected
- `/opt/homebrew/lib/node_modules/openclaw/docs/tools/subagents.md`
  - isolated sub-agent context expectations
  - deny-wins semantics for tool policy (`allow` + `deny`)
  - explicit sub-agent tool restriction model
- `/opt/homebrew/lib/node_modules/openclaw/docs/tools/multi-agent-sandbox-tools.md`
  - layered tool policy resolution and agent-specific policy enforcement intent
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/agent-scope.d.ts`
  - resolved per-agent config shape (`tools`, `subagents`, model/runtime scope)
- `/opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/tool-policy.d.ts`
  - normalized allow/deny policy utilities and deny precedence

## What was implemented

### 1) Runtime enforcement in agent execution path
- `TaskQueue` now persists/loads `context` and `tools` policy payloads on tasks.
- `Application` passes task runtime policy into `AgentEngine::routeInboundMessage(...)`.
- `AgentEngine` now consumes runtime policy and enforces:
  - **Context carryover/history** in prompt building:
    - `inherit`: full filtered history up to `historyLimit`
    - `minimal`: compact carryover (latest bounded user/assistant/system subset)
    - `none`: no carryover except current turn
  - **Tool restrictions** for `/tool` command calls:
    - deny wins (`run.options.tools.deny`)
    - allowlist mode when `allow` is present (with `*` support)

### 2) Explicit JSON-first policy validation/errors on task enqueue
`POST /api/tasks` now validates policy payload shape and returns structured errors:
- `invalid_context_policy`
- `invalid_context_history_limit`
- `invalid_context_carryover`
- `invalid_tools_policy`
- `invalid_tool_allow`
- `invalid_tool_deny`

Denied runtime tool calls now return structured result payload:
- `error: "tool_denied_by_runtime_policy"`
- `policyReason` with explicit source (`run.options.tools.*`)

### 3) Compatibility
- No breaking changes for callers that do not pass `context`/`tools` in tasks.
- Existing global/channel/peer tool policy in `ToolRegistry` remains intact.
- Unsupported/invalid paths remain explicit JSON errors.

## Smoke coverage
Added:
- `scripts/smoke_stage33_agents_policy_enforcement.sh`

Checks:
- runtime task-level tools deny blocks `/tool` call with explicit JSON error in task result
- invalid task context carryover rejected at enqueue
- invalid task tool allowlist rejected at enqueue

`smoke_full.sh` includes Stage33 smoke.
