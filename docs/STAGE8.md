# Stage 8 — Practical maximum polish (current)

## Ops/UX

- Doctor checks expanded:
  - config parse and auth mode validity
  - env requirements
  - workspace/state existence + state write permissions
  - browser backend readiness diagnostic
- Service templates:
  - `deploy/clawforge.service` (systemd)
  - `deploy/com.clawforge.agent.plist` (launchd)
- Scripts:
  - `scripts/bootstrap.sh`
  - `scripts/smoke_full.sh`
  - `scripts/benchmark_quick.sh`

## Final parity matrix (practical)

Closed now:
- Core API/session/tools/cron
- Scoped tools policy
- Browser relay baseline endpoints
- Basic orchestration lane with persistent state
- Rate limiting + audit trail

Still open vs full OpenClaw parity:
- real browser automation backend (snapshot/act)
- canvas/nodes/device control
- full multi-channel plugin ecosystem
- true subagent process orchestration across runtimes

Reason: these require heavier external runtimes/integrations beyond lightweight standalone C++ baseline.
