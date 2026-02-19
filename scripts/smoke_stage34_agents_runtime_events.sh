#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage34-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage34-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
cfg['http']['port']=18934
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" run --config "$CFG" > /tmp/nexaclaw-stage34.log 2>&1 &
PID=$!
trap 'kill $PID >/dev/null 2>&1 || true' EXIT
sleep 1

TASK=$(curl -fsS -X POST http://127.0.0.1:18934/api/tasks -H 'Content-Type: application/json' -d '{"channel":"api","peerId":"stage34","text":"/status"}')
echo "$TASK" | grep -q '"ok": true'
TASK_ID=$(echo "$TASK" | python3 -c 'import json,sys;print(json.load(sys.stdin)["task"]["id"])')

for _ in $(seq 1 40); do
  GOT=$(curl -fsS "http://127.0.0.1:18934/api/tasks/$TASK_ID")
  STATUS=$(echo "$GOT" | python3 -c 'import json,sys;print(json.load(sys.stdin)["task"]["status"])')
  if [[ "$STATUS" != "queued" && "$STATUS" != "running" && "$STATUS" != "cancelling" ]]; then
    break
  fi
  sleep 0.25
done

EVENTS=$(curl -fsS "http://127.0.0.1:18934/api/tasks/$TASK_ID/events?limit=20")
echo "$EVENTS" | grep -q '"ok": true'
echo "$EVENTS" | grep -q 'task_enqueued'
echo "$EVENTS" | grep -q 'task_started'
echo "$EVENTS" | grep -q 'task_finished'

BAD_LIMIT=$(curl -sS "http://127.0.0.1:18934/api/tasks/$TASK_ID/events?limit=0")
echo "$BAD_LIMIT" | grep -q '"error": "invalid_events_limit"'

AFTER=$(echo "$EVENTS" | python3 -c 'import json,sys;d=json.load(sys.stdin);print(d.get("nextAfterSeq",0))')
DELTA=$(curl -fsS "http://127.0.0.1:18934/api/tasks/$TASK_ID/events?afterSeq=$AFTER")
echo "$DELTA" | grep -q '"events": \[\]'

CANCEL_TERMINAL=$(curl -sS -X POST "http://127.0.0.1:18934/api/tasks/$TASK_ID/cancel")
echo "$CANCEL_TERMINAL" | grep -q '"error": "task_cancel_not_allowed"'

echo "Stage34 agents runtime events smoke: OK"
