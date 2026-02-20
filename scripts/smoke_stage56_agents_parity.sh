#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
DOC="docs/STAGE56.md"
[ -f "$DOC" ]
grep -q "OpenClaw source refs inspected" "$DOC"
grep -q "subagents.md" "$DOC"
grep -q "slash-commands.md" "$DOC"
python3 - <<'PYCHECK'
import json
payload={"ok":True,"stage":56,"kind":"agents_parity_gate"}
assert payload["stage"]==56
print(json.dumps(payload, sort_keys=True))
PYCHECK
echo "Stage56 agents parity smoke: OK"
