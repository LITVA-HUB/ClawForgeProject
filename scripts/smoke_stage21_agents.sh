#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage21-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage21-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" agents list --config "$CFG" | grep -q '"id": "main"'
"$BIN" agents create smoke-agent --name "Smoke Agent" --config "$CFG" | grep -q '"created": true'
"$BIN" agents use smoke-agent --config "$CFG" | grep -q '"active": "smoke-agent"'
"$BIN" agents show smoke-agent --config "$CFG" | grep -q '"sessionKey": "agent:smoke-agent"'
"$BIN" agent list --config "$CFG" | grep -q '"active": "smoke-agent"'
"$BIN" agents run smoke-agent --message "stage21 smoke" --config "$CFG" | grep -Eq '"mode": "(gateway-task|gateway-message|local-session)"'
"$BIN" agents delete smoke-agent --config "$CFG" | grep -q '"deleted": true'
STUB_OUT=$("$BIN" agents spawn --config "$CFG" || true)
echo "$STUB_OUT" | grep -q '"error": "not_implemented"'

echo "Stage21 agents/agent smoke: OK"
