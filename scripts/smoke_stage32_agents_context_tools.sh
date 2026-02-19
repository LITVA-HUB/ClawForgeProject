#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage32-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage32-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" agents create stage32-agent --name "Stage32 Agent" --config "$CFG" | grep -q '"created": true'
"$BIN" agents update stage32-agent --context-history-limit 42 --context-carryover minimal --tool-allow "read,write,read" --tool-deny "browser" --config "$CFG" | grep -q '"updated": true'
SHOW_JSON=$("$BIN" agents show stage32-agent --config "$CFG")
echo "$SHOW_JSON" | grep -q '"historyLimit": 42'
echo "$SHOW_JSON" | grep -q '"carryover": "minimal"'
echo "$SHOW_JSON" | grep -q '"allow": \['
echo "$SHOW_JSON" | grep -q '"deny": \['

echo "$SHOW_JSON" | grep -q '"read"'

echo "$SHOW_JSON" | grep -q '"browser"'

RUN_JSON=$("$BIN" agents run stage32-agent --message "stage32 context+tools" --context-history-limit 24 --tool-deny "browser,exec" --config "$CFG" || true)
echo "$RUN_JSON" | grep -q '"error": "advanced_options_require_gateway"'
echo "$RUN_JSON" | grep -q '"context"'
echo "$RUN_JSON" | grep -q '"tools"'

BAD=$("$BIN" agents update stage32-agent --context-carryover full --config "$CFG" || true)
echo "$BAD" | grep -q '"error": "invalid_context_carryover"'

echo "Stage32 agents context+tools smoke: OK"
