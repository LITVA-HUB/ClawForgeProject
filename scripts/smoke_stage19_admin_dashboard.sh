#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage19-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage19-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" run --config "$CFG" >/tmp/nexaclaw-stage19.log 2>&1 &
PID=$!
trap 'kill $PID >/dev/null 2>&1 || true' EXIT
sleep 1

curl -fsS http://127.0.0.1:18890/admin | grep -q 'NexaClaw Admin Dashboard'
curl -fsS http://127.0.0.1:18890/api/admin/overview | grep -q '"ok": true'
curl -fsS http://127.0.0.1:18890/api/admin/logs/tail?limit=5 | grep -q '"items"'
curl -fsS http://127.0.0.1:18890/api/admin/audit/tail?limit=5 | grep -q '"items"'

JOBS=$(curl -fsS http://127.0.0.1:18890/api/cron/jobs)
JOB_ID=$(python3 - <<'PY' "$JOBS"
import json,sys
jobs=json.loads(sys.argv[1]).get('jobs',[])
print(jobs[0]['id'] if jobs else '')
PY
)
if [[ -n "$JOB_ID" ]]; then
  curl -fsS -X POST http://127.0.0.1:18890/api/cron/jobs/$JOB_ID/run -H 'Content-Type: application/json' -d '{"mode":"force"}' | grep -q '"ok": true'
fi

echo "Stage19 admin dashboard smoke: OK"
