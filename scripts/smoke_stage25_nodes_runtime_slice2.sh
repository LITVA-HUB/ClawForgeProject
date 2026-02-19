#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage25-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage25-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" nodes list --config "$CFG" | grep -q '"runtime"'
"$BIN" nodes invoke --action probe --config "$CFG" | grep -q '"result": "runtime-probe"'
"$BIN" devices status --config "$CFG" | grep -q '"online"'
"$BIN" devices invoke --action metrics --config "$CFG" | grep -q '"runtime"'
"$BIN" canvas snapshot --config "$CFG" | grep -q '"kind": "virtual"'
"$BIN" gateway call canvas.snapshot --params '{}' --config "$CFG" | grep -q '"method": "canvas.snapshot"'

BAD=$("$BIN" canvas invoke --action rm --config "$CFG" || true)
echo "$BAD" | grep -q '"error": "canvas_action_not_allowed"'

echo "Stage25 nodes/canvas/devices runtime slice2 smoke: OK"
