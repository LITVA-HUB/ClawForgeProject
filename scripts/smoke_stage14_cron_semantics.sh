#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage14-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage14-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" cron status --config "$CFG" | grep -q '"ok": true'
"$BIN" cron validate --json '{"schedule":{"kind":"every","everyMs":1000},"sessionTarget":"main","payload":{"kind":"systemEvent","text":"ok"}}' --config "$CFG" | grep -q '"ok": true'

if "$BIN" cron validate --json '{"schedule":{"kind":"every","everyMs":1000},"sessionTarget":"main","payload":{"kind":"agentTurn","message":"x"}}' --config "$CFG" >/tmp/nexaclaw-stage14-invalid.json 2>&1; then
  echo "[FAIL] invalid cron payload unexpectedly accepted"
  exit 1
fi
grep -q "sessionTarget='main' requires payload.kind='systemEvent'" /tmp/nexaclaw-stage14-invalid.json

ADD_MAIN=$("$BIN" cron add --json '{"name":"stage14-main","schedule":{"kind":"every","everyMs":60000},"sessionTarget":"main","payload":{"kind":"systemEvent","text":"Reminder stage14"}}' --config "$CFG")
MAIN_ID=$(python3 - <<'PY' "$ADD_MAIN"
import json,sys
print(json.loads(sys.argv[1])['job']['id'])
PY
)

"$BIN" cron edit "$MAIN_ID" --json '{"payload":{"text":"Reminder stage14 updated"}}' --config "$CFG" | grep -q '"ok": true'
"$BIN" cron disable "$MAIN_ID" --config "$CFG" | grep -q '"enabled": false'
"$BIN" cron enable "$MAIN_ID" --config "$CFG" | grep -q '"enabled": true'
"$BIN" cron run "$MAIN_ID" --due --config "$CFG" | grep -q '"status": "skipped"'
"$BIN" cron run "$MAIN_ID" --config "$CFG" | grep -q '"status": "ok"'
"$BIN" cron runs "$MAIN_ID" --limit 5 --config "$CFG" | grep -q '"runs"'

ADD_ISO=$("$BIN" cron add --json '{"name":"stage14-iso","schedule":{"kind":"every","everyMs":60000},"sessionTarget":"isolated","payload":{"kind":"agentTurn","message":"Isolated stage14"}}' --config "$CFG")
echo "$ADD_ISO" | grep -q '"sessionTarget": "isolated"'
echo "$ADD_ISO" | grep -q '"mode": "announce"'

echo "Stage14 cron semantics smoke: OK"
