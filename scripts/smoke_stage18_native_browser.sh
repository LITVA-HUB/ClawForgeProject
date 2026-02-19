#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage18-native-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage18-native-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['browser']['backend']='native'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" browser status --config "$CFG" | grep -q '"backend": "native"'

OPEN_EXAMPLE=$("$BIN" browser open https://example.com --config "$CFG")
echo "$OPEN_EXAMPLE" | grep -q '"ok": true'
TID_EXAMPLE=$(python3 - <<'PY' "$OPEN_EXAMPLE"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
[[ -n "$TID_EXAMPLE" ]]

SNAP_EXAMPLE=$("$BIN" browser snapshot --target-id "$TID_EXAMPLE" --config "$CFG")
echo "$SNAP_EXAMPLE" | grep -q '"format": "ai"'
REF_LINK=$(python3 - <<'PY' "$SNAP_EXAMPLE"
import json,sys
j=json.loads(sys.argv[1])
refs=j.get('refs') or {}
chosen=''
for k,v in refs.items():
    if isinstance(v,dict) and v.get('role')=='link':
        chosen=k
        break
if not chosen and refs:
    chosen=next(iter(refs.keys()))
print(chosen)
PY
)
[[ -n "$REF_LINK" ]]
CLICK_OUT=$("$BIN" browser click "$REF_LINK" --target-id "$TID_EXAMPLE" --config "$CFG")
echo "$CLICK_OUT" | grep -q '"ok": true'
# click on native link should model action side-effect (url change when href is known)
if echo "$CLICK_OUT" | grep -q '"navigated": true'; then
  echo "$CLICK_OUT" | grep -q '"url": '
fi

OPEN_DATA=$("$BIN" browser open "data:text/html,%3Chtml%3E%3Cbody%3E%3Cinput%20aria-label%3D%27q%27/%3E%3C/body%3E%3C/html%3E" --config "$CFG")
TID_DATA=$(python3 - <<'PY' "$OPEN_DATA"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
[[ -n "$TID_DATA" ]]

SNAP_DATA=$("$BIN" browser snapshot --target-id "$TID_DATA" --config "$CFG")
REF_INPUT=$(python3 - <<'PY' "$SNAP_DATA"
import json,sys
j=json.loads(sys.argv[1])
refs=j.get('refs') or {}
chosen=''
for k,v in refs.items():
    if isinstance(v,dict) and v.get('role') in ('textbox','searchbox','combobox'):
        chosen=k
        break
print(chosen)
PY
)
[[ -n "$REF_INPUT" ]]
"$BIN" browser type "$REF_INPUT" "stage18" --target-id "$TID_DATA" --config "$CFG" | grep -q '"ok": true'
SNAP_DATA_2=$("$BIN" browser snapshot --target-id "$TID_DATA" --config "$CFG")
python3 - "$SNAP_DATA" "$SNAP_DATA_2" "$REF_INPUT" >/dev/null <<'PY'
import json,sys
s1=json.loads(sys.argv[1]); s2=json.loads(sys.argv[2]); ref=sys.argv[3]
r1=s1.get('refs',{}).get(ref,{})
r2=s2.get('refs',{}).get(ref,{})
assert r1 and r2, 'typed ref missing after second snapshot'
assert r2.get('role')==r1.get('role'), 'ref role changed unexpectedly'
assert r2.get('text')=='stage18', 'typed text not reflected into snapshot'
print('ok')
PY
"$BIN" browser screenshot "$TID_DATA" --config "$CFG" | grep -q '"path": '

# error path: unknown targetId for snapshot/click
if "$BIN" browser snapshot --target-id native-404 --config "$CFG" >/tmp/nexaclaw-stage18-native-bad-snap.json 2>&1; then
  echo "[FAIL] native snapshot unexpectedly succeeded for unknown target"
  exit 1
fi
grep -q '"code": "target_not_found"' /tmp/nexaclaw-stage18-native-bad-snap.json

if "$BIN" browser click e1 --target-id native-404 --config "$CFG" >/tmp/nexaclaw-stage18-native-bad-click.json 2>&1; then
  echo "[FAIL] native click unexpectedly succeeded for unknown target"
  exit 1
fi
grep -q '"code": "target_not_found"' /tmp/nexaclaw-stage18-native-bad-click.json

echo "Stage18 native browser smoke: OK"
