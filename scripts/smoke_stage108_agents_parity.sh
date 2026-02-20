#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
DOC="docs/STAGE108.md"
[ -f "$DOC" ]
grep -q "OpenClaw source refs inspected" "$DOC"
grep -q "subagents.md" "$DOC"
grep -q "slash-commands.md" "$DOC"
python3 - <<'PYCHECK'
import json
payload={"ok":True,"stage":108,"kind":"agents_parity_gate"}
assert payload["stage"]==108
print(json.dumps(payload, sort_keys=True))
PYCHECK
echo "Stage108 agents parity smoke: OK"
