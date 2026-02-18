#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage17-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage17-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

PORT=18999
MOCK_PID_FILE="/tmp/nexaclaw-stage17-oauth-mock.pid"
python3 - <<'PY' "$PORT" &
import json,sys
from http.server import BaseHTTPRequestHandler, HTTPServer
port=int(sys.argv[1])
class H(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_POST(self):
        ln=int(self.headers.get('Content-Length','0'))
        body=self.rfile.read(ln).decode('utf-8','replace')
        params={}
        for kv in body.split('&'):
            if '=' in kv:
                k,v=kv.split('=',1)
                params[k]=v
        dc=params.get('device_code','')
        if 'pending' in dc:
            self.send_response(400)
            self.send_header('Content-Type','application/json')
            self.end_headers()
            self.wfile.write(json.dumps({'error':'authorization_pending','error_description':'still waiting'}).encode())
            return
        self.send_response(200)
        self.send_header('Content-Type','application/json')
        self.end_headers()
        self.wfile.write(json.dumps({'access_token':'stage17-token','expires_in':1200,'token_type':'Bearer','scope':'model.read'}).encode())

HTTPServer(('127.0.0.1', port), H).serve_forever()
PY
MOCK_PID=$!
echo "$MOCK_PID" > "$MOCK_PID_FILE"
trap 'kill "$MOCK_PID" >/dev/null 2>&1 || true' EXIT
sleep 0.2

# 1) native mode should require device-code payload
if "$BIN" models auth login --provider openai-codex --config "$CFG" >/tmp/nx-stage17-no-device.json 2>&1; then
  echo "[FAIL] login without device code unexpectedly succeeded"
  exit 1
fi
grep -q 'Native device-code flow requires --device-code-json' /tmp/nx-stage17-no-device.json

# 2) native ready phase (non-poll)
READY=$("$BIN" models auth login --provider openai-codex --device-code-json '{"device_code":"dc-success","user_code":"ABCD-EFGH","verification_uri":"https://example.test/verify","interval":4,"expires_in":900}' --config "$CFG")
echo "$READY" | grep -q '"phase": "device_code_ready"'

# 3) poll retryable error path
set +e
"$BIN" models auth login --provider openai-codex --device-code-json '{"device_code":"pending"}' --poll --client-id stage17-client --token-url "http://127.0.0.1:$PORT/token" --config "$CFG" >/tmp/nx-stage17-pending.json 2>&1
RC=$?
set -e
if [[ "$RC" -ne 2 ]]; then
  echo "[FAIL] expected retryable poll exit code 2, got $RC"
  cat /tmp/nx-stage17-pending.json
  exit 1
fi
grep -q '"retryable": true' /tmp/nx-stage17-pending.json

# 4) poll success -> stored profile
OK=$("$BIN" models auth login --provider openai-codex --device-code-json '{"device_code":"dc-success"}' --poll --client-id stage17-client --token-url "http://127.0.0.1:$PORT/token" --profile-id stage17-native --config "$CFG")
echo "$OK" | grep -q '"ok": true'
echo "$OK" | grep -q '"native": true'
"$BIN" models auth list --config "$CFG" | grep -q 'stage17-native'

# 5) bridge fallback still works
AUTH_SAMPLE="/tmp/nexaclaw-stage17-openclaw-auth.json"
cat >"$AUTH_SAMPLE" <<'JSON'
{
  "version": 1,
  "profiles": {
    "openai-codex:default": {
      "type": "oauth",
      "provider": "openai-codex",
      "access": "stage17-bridge-token",
      "expires": "2099-01-01T00:00:00Z",
      "accountId": "acct-stage17"
    }
  }
}
JSON

BRIDGE=$("$BIN" models auth login --provider openai-codex --bridge-import --openclaw-auth-file "$AUTH_SAMPLE" --profile-id stage17-bridge --config "$CFG")
echo "$BRIDGE" | grep -q '"imported": true'
echo "$BRIDGE" | grep -q '"bridge": true'

echo "Stage17 native oauth smoke: OK"
