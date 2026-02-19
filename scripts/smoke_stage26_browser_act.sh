#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage26-browser-act-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage26-browser-act-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['browser']['backend']='native'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

OPEN=$("$BIN" browser open "data:text/html,%3Chtml%3E%3Cbody%3E%3Cform%20action%3D%27https%3A%2F%2Fexample.com%2Fsearch%27%20method%3D%27get%27%3E%3Cinput%20name%3D%27q%27%20aria-label%3D%27Query%27/%3E%3Cbutton%20type%3D%27submit%27%3EGo%3C/button%3E%3C/form%3E%3Ca%20href%3D%27https%3A%2F%2Fexample.com%2Fnext%27%3ENext%3C/a%3E%3C/body%3E%3C/html%3E" --config "$CFG")
TID=$(python3 - <<'PY' "$OPEN"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
[[ -n "$TID" ]]

SNAP=$("$BIN" browser snapshot --target-id "$TID" --config "$CFG")
REF_INPUT=$(python3 - <<'PY' "$SNAP"
import json,sys
refs=(json.loads(sys.argv[1]).get('refs') or {})
for k,v in refs.items():
    if isinstance(v,dict) and v.get('role') in ('textbox','searchbox'):
        print(k); break
else:
    print('')
PY
)
REF_LINK=$(python3 - <<'PY' "$SNAP"
import json,sys
refs=(json.loads(sys.argv[1]).get('refs') or {})
for k,v in refs.items():
    if isinstance(v,dict) and v.get('role')=='link':
        print(k); break
else:
    print('')
PY
)
[[ -n "$REF_INPUT" ]]
[[ -n "$REF_LINK" ]]

ACT_TYPE=$($BIN browser act --json "{\"kind\":\"type\",\"ref\":\"$REF_INPUT\",\"text\":\"stage26\"}" --target-id "$TID" --config "$CFG")
echo "$ACT_TYPE" | grep -q '"ok": true'

ACT_WAIT=$($BIN browser act --json '{"kind":"wait","timeMs":120}' --target-id "$TID" --config "$CFG")
echo "$ACT_WAIT" | grep -q '"kind": "wait"'

ACT_CLICK=$($BIN browser act --json "{\"kind\":\"click\",\"ref\":\"$REF_LINK\"}" --target-id "$TID" --config "$CFG")
echo "$ACT_CLICK" | grep -q '"ok": true'

ACT_PRESS=$($BIN browser act --json '{"kind":"press","key":"Enter"}' --target-id "$TID" --config "$CFG")
python3 - <<'PY' "$ACT_PRESS"
import json,sys
j=json.loads(sys.argv[1])
assert j.get('ok') is True, 'press should succeed'
assert j.get('kind') == 'press', 'kind missing'
print('ok')
PY

if "$BIN" browser act --json '{"kind":"drag","startRef":"e1","endRef":"e2"}' --target-id "$TID" --config "$CFG" >/tmp/nexaclaw-stage26-act-bad-kind.json 2>&1; then
  echo "[FAIL] act drag unexpectedly succeeded"
  exit 1
fi
grep -q '"error": "native_browser_act_kind_unsupported"' /tmp/nexaclaw-stage26-act-bad-kind.json

GW=$($BIN gateway call browser.act --params '{"request":{"kind":"wait","timeMs":1}}' --config "$CFG")
echo "$GW" | grep -q '"ok": true'

echo "Stage26 browser act smoke: OK"
