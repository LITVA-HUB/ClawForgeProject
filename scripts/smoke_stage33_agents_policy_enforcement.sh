#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage33-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage33-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
cfg['http']['port']=18933
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" run --config "$CFG" > /tmp/nexaclaw-stage33.log 2>&1 &
PID=$!
trap 'kill $PID >/dev/null 2>&1 || true' EXIT
sleep 1

DENIED_PAYLOAD=$(python3 - <<'PY'
import json
print(json.dumps({
  'channel':'api',
  'peerId':'stage33',
  'text':'/tool read {"path":"config/config.example.json"}',
  'tools':{'deny':['read']}
}))
PY
)
DENIED=$(curl -fsS -X POST http://127.0.0.1:18933/api/tasks -H 'Content-Type: application/json' -d "$DENIED_PAYLOAD")
echo "$DENIED" | grep -q '"ok": true'
TASK_ID=$(echo "$DENIED" | python3 -c 'import json,sys;print(json.load(sys.stdin)["task"]["id"])')
sleep 1
RESULT=$(curl -fsS "http://127.0.0.1:18933/api/tasks/$TASK_ID")
echo "$RESULT" | grep -q 'tool_denied_by_runtime_policy'

echo "$RESULT" | grep -q 'run.options.tools.deny'

BAD_CONTEXT=$(curl -sS -X POST http://127.0.0.1:18933/api/tasks -H 'Content-Type: application/json' -d '{"channel":"api","peerId":"stage33","text":"/status","context":{"carryover":"full"}}')
echo "$BAD_CONTEXT" | grep -q '"error": "invalid_context_carryover"'

BAD_TOOLS=$(curl -sS -X POST http://127.0.0.1:18933/api/tasks -H 'Content-Type: application/json' -d '{"channel":"api","peerId":"stage33","text":"/status","tools":{"allow":[""]}}')
echo "$BAD_TOOLS" | grep -q '"error": "invalid_tool_allow"'

echo "Stage33 agents policy enforcement smoke: OK"
