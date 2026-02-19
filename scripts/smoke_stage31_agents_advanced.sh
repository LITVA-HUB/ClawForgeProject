#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage31-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage31-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" agents create stage31-agent --name "Stage31 Agent" --config "$CFG" | grep -q '"created": true'
"$BIN" agents update stage31-agent --profile research --role orchestrator --tags "ops,qa" --subagent-model gpt-5 --subagent-thinking low --allow-agents "main,worker" --max-concurrent 3 --archive-after-minutes 90 --config "$CFG" | grep -q '"updated": true'
"$BIN" agents show stage31-agent --config "$CFG" | grep -q '"profile": "research"'

RUN_JSON=$("$BIN" agents run stage31-agent --message "stage31 baseline run" --config "$CFG")
echo "$RUN_JSON" | grep -Eq '"status": "(queued|completed|stored)"'
RUN_ID=$(echo "$RUN_JSON" | python3 -c 'import sys,json; print(json.load(sys.stdin).get("run",{}).get("runId",""))')
[[ -n "$RUN_ID" ]]

"$BIN" agents run-status "$RUN_ID" --config "$CFG" | grep -q '"runId":'
"$BIN" agents runs stage31-agent --status stored --limit 5 --config "$CFG" | grep -q '"subcommand": "runs"'

BAD=$("$BIN" agents update stage31-agent --subagent-thinking turbo --config "$CFG" || true)
echo "$BAD" | grep -q '"error": "invalid_subagent_thinking"'

echo "Stage31 advanced agents smoke: OK"
