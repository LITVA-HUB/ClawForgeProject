#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage27-browser-act-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage27-browser-act-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['browser']['backend']='native'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

OPEN=$($BIN browser open "data:text/html,%3Chtml%3E%3Cbody%3E%3Cform%20action%3D%27https%3A%2F%2Fexample.com%2Fsearch%27%20method%3D%27get%27%3E%3Cinput%20name%3D%27q%27%20aria-label%3D%27Query%27/%3E%3Cbutton%20type%3D%27submit%27%3EGo%3C/button%3E%3C/form%3E%3Ca%20href%3D%27https%3A%2F%2Fexample.com%2Fnext%27%3ENext%3C/a%3E%3C/body%3E%3C/html%3E" --config "$CFG")
TID=$(python3 - <<'PY' "$OPEN"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
[[ -n "$TID" ]]

SNAP=$($BIN browser snapshot --target-id "$TID" --config "$CFG")
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

ACT_HOVER=$($BIN browser act --json "{\"kind\":\"hover\",\"ref\":\"$REF_LINK\"}" --target-id "$TID" --config "$CFG")
echo "$ACT_HOVER" | grep -q '"ok": true'
echo "$ACT_HOVER" | grep -q '"kind": "hover"'

ACT_SCROLL=$($BIN browser act --json "{\"kind\":\"scrollIntoView\",\"ref\":\"$REF_LINK\"}" --target-id "$TID" --config "$CFG")
echo "$ACT_SCROLL" | grep -q '"ok": true'

ACT_FILL=$($BIN browser act --json "{\"kind\":\"fill\",\"fields\":[{\"ref\":\"$REF_INPUT\",\"value\":\"stage27\"}]}" --target-id "$TID" --config "$CFG")
echo "$ACT_FILL" | grep -q '"ok": true'

ACT_RESIZE=$($BIN browser act --json '{"kind":"resize","width":1024,"height":640}' --target-id "$TID" --config "$CFG")
echo "$ACT_RESIZE" | grep -q '"ok": true'
SNAP2=$($BIN browser snapshot --target-id "$TID" --config "$CFG")
echo "$SNAP2" | grep -q '"viewport"'
echo "$SNAP2" | grep -q '"width": 1024'

if "$BIN" browser act --json '{"kind":"press","key":"Escape"}' --target-id "$TID" --config "$CFG" >/tmp/nexaclaw-stage27-act-press-unsupported.json 2>&1; then
  echo "[FAIL] native press Escape unexpectedly succeeded"
  exit 1
fi
grep -q '"code": "native_capability_press_key_unsupported"' /tmp/nexaclaw-stage27-act-press-unsupported.json

ACT_EVAL=$($BIN browser act --json '{"kind":"evaluate","fn":"() => location.href"}' --target-id "$TID" --config "$CFG")
echo "$ACT_EVAL" | grep -q '"ok": true'
echo "$ACT_EVAL" | grep -q '"kind": "evaluate"'

ACT_EVAL_NAV=$($BIN browser act --json '{"kind":"evaluate","fn":"() => { location.href = \"https://example.com/eval\"; return location.href; }"}' --target-id "$TID" --config "$CFG")
echo "$ACT_EVAL_NAV" | grep -q '"ok": true'
echo "$ACT_EVAL_NAV" | grep -q 'https://example.com/eval'
SNAP3=$($BIN browser snapshot --target-id "$TID" --config "$CFG")
echo "$SNAP3" | grep -q 'https://example.com/eval'

if "$BIN" browser act --json '{"kind":"evaluate","fn":"async () => 1"}' --target-id "$TID" --config "$CFG" >/tmp/nexaclaw-stage27-act-evaluate-async-unsupported.json 2>&1; then
  echo "[FAIL] native async evaluate unexpectedly succeeded"
  exit 1
fi
grep -q '"code": "native_capability_evaluate_async_unsupported"' /tmp/nexaclaw-stage27-act-evaluate-async-unsupported.json

ACT_CLOSE=$($BIN browser act --json '{"kind":"close"}' --target-id "$TID" --config "$CFG")
echo "$ACT_CLOSE" | grep -q '"ok": true'
echo "$ACT_CLOSE" | grep -q '"closed": true'

echo "Stage27 browser act kinds smoke: OK"
