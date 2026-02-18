# Stage 12 — Critical Gap Analysis (OpenClaw -> NexaClaw)

Audit date: 2026-02-18

## Sources re-checked
- OpenClaw CLI reference: `/opt/homebrew/lib/node_modules/openclaw/docs/cli/index.md`
- OpenClaw config docs: `/opt/homebrew/lib/node_modules/openclaw/docs/gateway/configuration.md`
- OpenClaw cron docs: `/opt/homebrew/lib/node_modules/openclaw/docs/automation/cron-jobs.md`
- OpenClaw concepts: agent/session/subagents docs
- Current NexaClaw implementation: `src/main.cpp`, `src/http/HttpServer.cpp`, config/scripts/docs

---

## Snapshot (now)

- OpenClaw top-level command surface in docs: ~40 commands.
- NexaClaw currently has:
  - ~13 top-level commands with real behavior (`status/health/doctor/sessions/gateway/security/cron/tools/browser/config/models/logs/system/pairing` + image-fallbacks)
  - compatibility stubs for the rest (40/40 top-level names recognized; practical coverage increased in Stage 12).
- Practical baseline is strong for local single-agent + Telegram + cron/tools/models routing.
- Full OpenClaw parity is still blocked by several architectural gaps.

---

## P0 (critical) — must close for "OpenClaw-like" reality

## 1) Gateway operations parity (service + RPC)
**Current:** Stage 12 baseline landed (`gateway status|start|stop|restart|health|call`).

**Remaining gap:** still partial versus OpenClaw service/runtime ecosystem.

**Why critical:** OpenClaw treats gateway lifecycle + RPC as the control plane (`gateway status/install/start/stop/restart`, `gateway call`, `config.apply/patch`, `update.run`).

**Next target:**
- add `gateway install/uninstall` parity baseline (service templates + UX)
- make `config.apply/config.patch` restart fully in-process and deterministic
- expand `gateway call` method coverage + explicit error taxonomy

---

## 2) Message/channel action parity
**Gap:** `message` command is stubbed; no unified outbound action surface.

**Why critical:** OpenClaw’s multi-channel usefulness depends on direct send/edit/react/poll style commands and structured targets.

**Minimum target:**
- `nexaclaw message send --channel <...> --target <...> --message <...>`
- Baseline `react` + `delete` for channels that support it
- Strict target format validation (no silent ambiguity)

---

## 3) Cron parity semantics (beyond add/list/run/rm)
**Gap:** current cron baseline misses key OpenClaw semantics.

**Why critical:** scheduler is one of the highest-value OpenClaw automation features.

**Missing critical pieces:**
- `cron status`, `cron edit`, `cron enable/disable`, `cron runs`
- `sessionTarget` contract (`main -> systemEvent`, `isolated -> agentTurn`)
- delivery controls for isolated runs (`announce|none`, `channel/to`, best-effort)
- wake modes parity (`now` vs `next-heartbeat`)

---

## 4) Model auth parity (real OAuth flow)
**Gap:** Stage 11 provides manual token storage; no true device-code OAuth flow.

**Why critical:** user-facing auth parity (especially codex/oauth-like UX) is explicitly requested.

**Minimum target:**
- Device-code login flow (start/poll/complete)
- Expiry handling + refresh path where provider supports it
- Profile order/priority controls (`auth order get/set/clear` baseline)

---

## 5) Browser backend parity
**Gap:** browser snapshot/actions remain diagnostic stub.

**Why critical:** OpenClaw browser tooling is major differentiator for automation tasks.

**Minimum target:**
- Real backend (Playwright/CDP)
- `snapshot` with stable refs
- At least `open/click/type/navigate/screenshot` end-to-end operationally

---

## 6) Secure DM/session isolation + security audit baseline
**Current:** Stage 12 baseline landed (`security audit [--deep] [--fix]`) with dmScope/auth/perms checks.

**Remaining gap:** still partial versus OpenClaw secure-DM depth.

**Why critical:** in multi-user inbox setups, weak DM scoping can leak context across users; OpenClaw treats this as a first-class security check.

**Next target:**
- add session scope parity for shared inboxes (`per-account-channel-peer` equivalent)
- deepen audit checks (multi-sender leakage heuristics, sandbox risk checks, auth profile hygiene)
- add structured machine-readable `--json` security report for CI

---

## P1 (high) — next after P0

1. `agent` / `agents` command family (isolated agents, add/list/delete baseline)
2. `channels` command family (status/list/add/remove for at least Telegram+Discord baseline)
3. `memory status/index/search` local semantic memory tooling
4. `sessions` enhancement (filters, active window, richer metadata)
5. `status --deep/--usage` parity surface

---

## P2 (important but not immediate blockers)

1. Plugin runtime parity (`plugins` management)
2. Nodes/Canvas/device control families (`node/nodes/devices`)
3. Hooks/webhooks integrations
4. Dashboard/TUI-level UX parity

---

## Recommended Stage 12 execution order

1. ✅ **Gateway + config apply/patch RPC baseline**
2. **Cron semantic parity (`status/edit/enable/disable/runs` + sessionTarget/delivery)**
3. ✅ **Secure DM/session isolation + `security audit` baseline**
4. **Message send baseline + channel target validation**
5. **OAuth device-code flow for `openai-codex`**
6. **Browser real backend milestone (Playwright)**

This order maximizes operational control first, then automation reliability + safety, then auth/browser UX.

---

## Quick parity KPI to track per stage

- Real implemented top-level commands (%)
- Implemented key subcommands in OpenClaw command tree
- End-to-end smoke coverage for each newly implemented command branch
- Number of remaining stubs in `compatTop`
