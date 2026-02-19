#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage22-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage22-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" nodes list --config "$CFG" | grep -q '"id": "local-node"'
"$BIN" nodes status --config "$CFG" | grep -q '"connected": 1'
"$BIN" node describe local-node --config "$CFG" | grep -q '"ok": true'
"$BIN" devices list --config "$CFG" | grep -q '"type": "paired-node"'
"$BIN" canvas status --config "$CFG" | grep -q '"mode": "baseline-stub"'
"$BIN" gateway call nodes.list --params '{}' --config "$CFG" | grep -q '"method": "nodes.list"'
"$BIN" gateway call canvas.status --params '{}' --config "$CFG" | grep -q '"method": "canvas.status"'

INVOKE_OUT=$("$BIN" nodes invoke --action reboot --config "$CFG" || true)
echo "$INVOKE_OUT" | grep -q '"error": "invoke_not_available_in_baseline"'

echo "Stage22 nodes/canvas/devices smoke: OK"
