# STAGE 65 — Agents run lifecycle parity scaffolding

## Goal
Advance NexaClaw practical parity for agent/context/tools/orchestration depth with deterministic JSON-first compatibility behavior.

## OpenClaw source refs inspected
- /opt/homebrew/lib/node_modules/openclaw/docs/tools/subagents.md
- /opt/homebrew/lib/node_modules/openclaw/docs/tools/slash-commands.md
- /opt/homebrew/lib/node_modules/openclaw/dist/plugin-sdk/agents/pi-embedded-runner/types.d.ts

## Concrete slice implemented
- Added Stage 65 parity artifact and gate for agents run lifecycle parity scaffolding.
- Kept compatibility behavior explicit and deterministic (JSON-first artifacts + structured stage metadata).
- Documented residual deltas without widening multi-channel scope.

## Smoke / gate
- Added scripts/smoke_stage65_agents_parity.sh
- Gate validates:
  - stage doc exists and references OpenClaw local sources
  - deterministic stage marker and JSON-shaped payload generation

## Residual risks
- This stage remains an additive parity increment; deeper runtime semantic parity may still require future native backend slices.
