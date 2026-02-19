#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

if ! command -v openclaw >/dev/null 2>&1; then
  echo "Stage16 smoke: SKIP (openclaw CLI not found)"
  exit 0
fi

run_browser_retry() {
  local attempts="$1"; shift
  local sleep_s="$1"; shift
  local i out rc
  for ((i=1;i<=attempts;i++)); do
    set +e
    out=$("$BIN" "$@" 2>&1)
    rc=$?
    set -e
    if [[ $rc -eq 0 ]]; then
      printf '%s' "$out"
      return 0
    fi
    if ! grep -Eq 'target_not_found|timeout|not attached|no tab|temporar|busy' <<<"$out"; then
      printf '%s\n' "$out" >&2
      return $rc
    fi
    if [[ $i -lt $attempts ]]; then
      sleep "$sleep_s"
      sleep_s=$(python3 - <<'PY' "$sleep_s"
import sys
print(min(float(sys.argv[1])*2.0, 1.6))
PY
)
    fi
  done
  printf '%s\n' "$out" >&2
  return 1
}

CFG="/tmp/nexaclaw-smoke-stage16-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage16-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['browser']['backend']='openclaw_cli'
cfg['browser']['profile']='openclaw'
cfg['browser']['cliBinary']='openclaw'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

run_browser_retry 3 0.2 browser status --config "$CFG" | grep -q '"backend": "openclaw_cli"'

OPEN_EXAMPLE=$(run_browser_retry 3 0.2 browser open https://example.com --config "$CFG")
echo "$OPEN_EXAMPLE" | grep -q '"ok": true'
TID_EXAMPLE=$(python3 - <<'PY' "$OPEN_EXAMPLE"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
if [[ -z "$TID_EXAMPLE" ]]; then
  echo "[FAIL] browser open did not return targetId"
  exit 1
fi

SNAP_EXAMPLE=$(run_browser_retry 4 0.2 browser snapshot --target-id "$TID_EXAMPLE" --config "$CFG")
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
if [[ -z "$REF_LINK" ]]; then
  echo "[FAIL] browser snapshot did not return refs"
  exit 1
fi

run_browser_retry 3 0.2 browser click "$REF_LINK" --target-id "$TID_EXAMPLE" --config "$CFG" | grep -q '"ok": true'

OPEN_DATA=$(run_browser_retry 3 0.2 browser open "data:text/html,%3Chtml%3E%3Cbody%3E%3Cinput%20aria-label%3D%27q%27/%3E%3C/body%3E%3C/html%3E" --config "$CFG")
echo "$OPEN_DATA" | grep -q '"ok": true'
TID_DATA=$(python3 - <<'PY' "$OPEN_DATA"
import json,sys
print(json.loads(sys.argv[1]).get('targetId',''))
PY
)
if [[ -z "$TID_DATA" ]]; then
  echo "[FAIL] data open did not return targetId"
  exit 1
fi

SNAP_DATA=$(run_browser_retry 4 0.2 browser snapshot --target-id "$TID_DATA" --config "$CFG")
REF_INPUT=$(python3 - <<'PY' "$SNAP_DATA"
import json,sys
j=json.loads(sys.argv[1])
refs=j.get('refs') or {}
chosen=''
for k,v in refs.items():
    if isinstance(v,dict) and v.get('role') in ('textbox','searchbox','combobox'):
        chosen=k
        break
if not chosen and refs:
    chosen=next(iter(refs.keys()))
print(chosen)
PY
)
if [[ -z "$REF_INPUT" ]]; then
  echo "[FAIL] data snapshot did not return input ref"
  exit 1
fi

run_browser_retry 3 0.2 browser type "$REF_INPUT" "stage16" --target-id "$TID_DATA" --config "$CFG" | grep -q '"ok": true'
run_browser_retry 3 0.2 browser screenshot "$TID_DATA" --config "$CFG" | grep -q '"path": '

# browser error path (invalid cli binary)
CFG_BAD="/tmp/nexaclaw-smoke-stage16-config-bad.json"
python3 - <<'PY' "$CFG" "$CFG_BAD"
import json,sys
j=json.load(open(sys.argv[1]))
j['browser']['cliBinary']='/definitely-missing-openclaw-binary'
j.setdefault('http', {})['port'] = 65521
json.dump(j, open(sys.argv[2], 'w'), indent=2)
PY
if "$BIN" browser status --config "$CFG_BAD" >/tmp/nexaclaw-stage16-browser-bad.json 2>&1; then
  echo "[FAIL] browser status unexpectedly succeeded with invalid cliBinary"
  exit 1
fi
grep -q 'openclaw browser command failed' /tmp/nexaclaw-stage16-browser-bad.json

# OAuth import + order baseline without interactive login
AUTH_SAMPLE="/tmp/nexaclaw-stage16-openclaw-auth.json"
cat >"$AUTH_SAMPLE" <<'JSON'
{
  "version": 1,
  "profiles": {
    "openai-codex:default": {
      "type": "oauth",
      "provider": "openai-codex",
      "access": "stage16-access-token",
      "refresh": "stage16-refresh-token",
      "expires": "2099-01-01T00:00:00Z",
      "accountId": "acct-stage16"
    }
  }
}
JSON

"$BIN" models auth login --provider openai-codex --openclaw-auth-file "$AUTH_SAMPLE" --profile-id imported-stage16 --config "$CFG" | grep -q '"imported": true'
"$BIN" models auth order set --provider openai-codex --profile-id imported-stage16 --config "$CFG" | grep -q '"ok": true'
"$BIN" models auth order get --provider openai-codex --config "$CFG" | grep -q 'imported-stage16'
"$BIN" models auth order clear --provider openai-codex --config "$CFG" | grep -q '"ok": true'

# message actions baseline dry-run
"$BIN" channels add --channel telegram --dm-policy pairing --config "$CFG" | grep -q '"ok": true'
"$BIN" message react --channel telegram --target @example_user --message-id 123 --emoji ✅ --dry-run --config "$CFG" | grep -q '"action": "message.react"'
"$BIN" message delete --channel telegram --target @example_user --message-id 123 --dry-run --config "$CFG" | grep -q '"action": "message.delete"'
"$BIN" message poll --channel telegram --target @example_user --poll-question "stage16?" --poll-option yes --poll-option no --dry-run --config "$CFG" | grep -q '"action": "message.poll"'


echo "Stage16 browser/oauth/message smoke: OK"
