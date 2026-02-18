#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG=/tmp/nexaclaw-smoke-stage12-config.json
cp config/config.example.json "$CFG"
python3 - <<'PY'
import json
p='/tmp/nexaclaw-smoke-stage12-config.json'
with open(p) as f:
    j=json.load(f)
j['http']['port']=18991
j['stateDir']='/tmp/nexaclaw-smoke-stage12-state'
with open(p,'w') as f:
    json.dump(j,f,indent=2)
PY

"$BIN" gateway status --config "$CFG" >/tmp/nexaclaw-stage12-gateway-status.json
grep -q '"running"' /tmp/nexaclaw-stage12-gateway-status.json

"$BIN" gateway start --config "$CFG" >/tmp/nexaclaw-stage12-gateway-start.json
grep -q '"ok": true' /tmp/nexaclaw-stage12-gateway-start.json
"$BIN" gateway health --config "$CFG" >/tmp/nexaclaw-stage12-gateway-health.json
grep -q '"ok": true' /tmp/nexaclaw-stage12-gateway-health.json
"$BIN" gateway stop --config "$CFG" >/tmp/nexaclaw-stage12-gateway-stop.json
grep -q '"ok": true' /tmp/nexaclaw-stage12-gateway-stop.json

"$BIN" gateway call config.get --config "$CFG" >/tmp/nexaclaw-stage12-config-get.json
grep -q '"hash"' /tmp/nexaclaw-stage12-config-get.json

"$BIN" gateway call config.patch --params '{"raw":"{\"api\":{\"dmScope\":\"per-peer\"}}"}' --config "$CFG" | grep -q '"ok": true'
"$BIN" config get api.dmScope --config "$CFG" | grep -q '^per-peer$'

"$BIN" config set api.dmScope main --config "$CFG" >/dev/null
"$BIN" security audit --config "$CFG" >/tmp/nexaclaw-stage12-security.txt
grep -q '"warnings"' /tmp/nexaclaw-stage12-security.txt

"$BIN" security audit --fix --config "$CFG" >/tmp/nexaclaw-stage12-security-fix.txt
grep -q '"fixed"' /tmp/nexaclaw-stage12-security-fix.txt
"$BIN" config get api.dmScope --config "$CFG" | grep -q '^per-channel-peer$'

echo "Stage12 gateway/security smoke: OK"
