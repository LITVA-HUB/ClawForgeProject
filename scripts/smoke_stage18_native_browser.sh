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

STATUS_OUT=$("$BIN" browser status --config "$CFG")
echo "$STATUS_OUT" | grep -q '"backend": "native"'
HTTP_FETCH_AVAILABLE=$(python3 - <<'PY' "$STATUS_OUT"
import json,sys
j=json.loads(sys.argv[1])
print('1' if bool((j.get('nativeRuntime') or {}).get('httpFetch')) else '0')
PY
)

OPEN_EXAMPLE=$("$BIN" browser open https://example.com --config "$CFG")
echo "$OPEN_EXAMPLE" | grep -q '"ok": true'
python3 - <<'PY' "$OPEN_EXAMPLE"
import json,sys
j=json.loads(sys.argv[1])
r=j.get('runtime') or {}
assert r.get('source') in ('http_fetch','url_only','data_url'), 'runtime source missing on open'
print('ok')
PY
TID_EXAMPLE=$(python3 - <<'PY' "$OPEN_EXAMPLE"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
[[ -n "$TID_EXAMPLE" ]]

SNAP_EXAMPLE=$("$BIN" browser snapshot --target-id "$TID_EXAMPLE" --config "$CFG")
echo "$SNAP_EXAMPLE" | grep -q '"format": "ai"'
python3 - <<'PY' "$OPEN_EXAMPLE" "$SNAP_EXAMPLE" "$HTTP_FETCH_AVAILABLE"
import json,sys
o=json.loads(sys.argv[1]); s=json.loads(sys.argv[2]); fetch=sys.argv[3]=='1'
for j in (o,s):
    r=j.get('runtime') or {}
    assert r.get('source') in ('http_fetch','url_only','data_url'), 'runtime source missing'
if not fetch:
    w=(o.get('runtime') or {}).get('warning') or (s.get('runtime') or {}).get('warning') or {}
    if w:
        assert str(w.get('code','')).startswith('native_runtime_'), 'unexpected warning code'
if fetch and (s.get('runtime') or {}).get('source')=='http_fetch':
    assert s.get('title'), 'expected non-empty title in http_fetch mode'
print('ok')
PY
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

# form submit fidelity: submit=true on textbox should navigate via GET action with query params
OPEN_FORM=$("$BIN" browser open "data:text/html,%3Chtml%3E%3Cbody%3E%3Cform%20action%3D%27https%3A%2F%2Fexample.com%2Fsearch%27%20method%3D%27get%27%3E%3Cinput%20name%3D%27q%27%20aria-label%3D%27Query%27/%3E%3Cbutton%20type%3D%27submit%27%3EGo%3C/button%3E%3C/form%3E%3C/body%3E%3C/html%3E" --config "$CFG")
TID_FORM=$(python3 - <<'PY' "$OPEN_FORM"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
[[ -n "$TID_FORM" ]]
SNAP_FORM=$("$BIN" browser snapshot --target-id "$TID_FORM" --config "$CFG")
REF_FORM_INPUT=$(python3 - <<'PY' "$SNAP_FORM"
import json,sys
refs=(json.loads(sys.argv[1]).get('refs') or {})
for k,v in refs.items():
    if isinstance(v,dict) and v.get('role') in ('textbox','searchbox'):
        print(k); break
else:
    print('')
PY
)
[[ -n "$REF_FORM_INPUT" ]]
TYPE_SUBMIT_OUT=$("$BIN" browser type "$REF_FORM_INPUT" "nexa stage24" --target-id "$TID_FORM" --submit --config "$CFG")
echo "$TYPE_SUBMIT_OUT" | grep -q '"submitted": true'
python3 - <<'PY' "$TYPE_SUBMIT_OUT"
import json,sys
j=json.loads(sys.argv[1])
assert j.get('navigated') is True, 'expected navigated=true'
assert 'q=nexa+stage24' in (j.get('url') or ''), 'expected encoded GET query in resulting url'
print('ok')
PY

# capability gate: unsupported form method should return structured error code
OPEN_FORM_POST=$("$BIN" browser open "data:text/html,%3Chtml%3E%3Cbody%3E%3Cform%20action%3D%27https%3A%2F%2Fexample.com%2Fsearch%27%20method%3D%27post%27%3E%3Cinput%20name%3D%27q%27%20aria-label%3D%27Query%27/%3E%3Cbutton%20type%3D%27submit%27%3EGo%3C/button%3E%3C/form%3E%3C/body%3E%3C/html%3E" --config "$CFG")
TID_FORM_POST=$(python3 - <<'PY' "$OPEN_FORM_POST"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
SNAP_FORM_POST=$("$BIN" browser snapshot --target-id "$TID_FORM_POST" --config "$CFG")
REF_FORM_POST_INPUT=$(python3 - <<'PY' "$SNAP_FORM_POST"
import json,sys
refs=(json.loads(sys.argv[1]).get('refs') or {})
for k,v in refs.items():
    if isinstance(v,dict) and v.get('role') in ('textbox','searchbox'):
        print(k); break
else:
    print('')
PY
)
if "$BIN" browser type "$REF_FORM_POST_INPUT" "blocked" --target-id "$TID_FORM_POST" --submit --config "$CFG" >/tmp/nexaclaw-stage18-native-bad-form-method.json 2>&1; then
  echo "[FAIL] native form submit unexpectedly succeeded for unsupported method"
  exit 1
fi
grep -q '"code": "native_capability_form_method_unsupported"' /tmp/nexaclaw-stage18-native-bad-form-method.json

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
