#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage23-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage23-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['http']['port']=18923
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

ADD=$("$BIN" cron add --json '{"name":"stage23-get","schedule":{"kind":"every","everyMs":60000},"sessionTarget":"main","payload":{"kind":"systemEvent","text":"stage23"}}' --config "$CFG")
ID=$(python3 - <<'PY' "$ADD"
import json,sys
print(json.loads(sys.argv[1])['job']['id'])
PY
)

"$BIN" cron get "$ID" --config "$CFG" | grep -q '"id":' 
"$BIN" cron show "$ID" --config "$CFG" | grep -q '"ok": true'

"$BIN" gateway probe --config "$CFG" | grep -q '"probes"'
"$BIN" gateway probe --url "http://127.0.0.1:1" --config "$CFG" | grep -q '"target": "explicit"'

"$BIN" gateway call logs.tail --params '{"lines":3}' --config "$CFG" | grep -q '"method": "logs.tail"'

"$BIN" gateway discover --config "$CFG" >/tmp/nexaclaw-stage23-gateway-discover.json || true
grep -q '"error": "not_implemented"' /tmp/nexaclaw-stage23-gateway-discover.json

"$BIN" security scan --config "$CFG" >/tmp/nexaclaw-stage23-security-scan.json || true
grep -q '"error": "not_implemented"' /tmp/nexaclaw-stage23-security-scan.json

echo "Stage23 control-plane slice1 smoke: OK"
