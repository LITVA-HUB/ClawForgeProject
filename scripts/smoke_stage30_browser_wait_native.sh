#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage30-browser-wait-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage30-browser-wait-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['browser']['backend']='native'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

URL='data:text/html,%3Chtml%3E%3Chead%3E%3Ctitle%3EStage30%20Wait%3C%2Ftitle%3E%3C%2Fhead%3E%3Cbody%3E%3Cp%3EReady%20State%3C%2Fp%3E%3C%2Fbody%3E%3C%2Fhtml%3E'
OPEN=$($BIN browser open "$URL" --config "$CFG")
TID=$(python3 - <<'PY' "$OPEN"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
[[ -n "$TID" ]]

WAIT_TEXT=$($BIN browser act --json '{"kind":"wait","text":"ready state","timeoutMs":200}' --target-id "$TID" --config "$CFG")
echo "$WAIT_TEXT" | grep -q '"ok": true'
echo "$WAIT_TEXT" | grep -q '"kind": "wait"'

WAIT_GONE=$($BIN browser act --json '{"kind":"wait","textGone":"this text is absent","timeoutMs":200}' --target-id "$TID" --config "$CFG")
echo "$WAIT_GONE" | grep -q '"ok": true'

a=$($BIN browser act --json '{"kind":"wait","textGone":"ready state","timeoutMs":120}' --target-id "$TID" --config "$CFG" >/tmp/nexaclaw-stage30-wait-gone-timeout.json 2>&1 || true)
if [[ -n "$a" ]]; then :; fi
grep -q '"code": "native_wait_text_gone_timeout"' /tmp/nexaclaw-stage30-wait-gone-timeout.json

if "$BIN" browser act --json '{"kind":"wait","selector":"#main"}' --target-id "$TID" --config "$CFG" >/tmp/nexaclaw-stage30-wait-selector-unsupported.json 2>&1; then
  echo "[FAIL] native wait selector unexpectedly succeeded"
  exit 1
fi
grep -q '"code": "native_capability_wait_selector_unsupported"' /tmp/nexaclaw-stage30-wait-selector-unsupported.json

echo "Stage30 native browser wait smoke: OK"
