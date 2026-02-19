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
python3 - <<'PY' "$PORT" &
import json,sys,urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer
port=int(sys.argv[1])
class H(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass
    def do_POST(self):
        ln=int(self.headers.get('Content-Length','0'))
        body=self.rfile.read(ln).decode('utf-8','replace')
        params={k:v[0] for k,v in urllib.parse.parse_qs(body, keep_blank_values=True).items()}
        if self.path == '/device/code':
            cid=params.get('client_id','')
            if cid == 'bad-client':
                self.send_response(400)
                self.send_header('Content-Type','application/json')
                self.end_headers()
                self.wfile.write(json.dumps({'error':'invalid_client','error_description':'bad client id'}).encode())
                return
            self.send_response(200)
            self.send_header('Content-Type','application/json')
            self.end_headers()
            self.wfile.write(json.dumps({
                'device_code':'dc-started',
                'user_code':'ZZZZ-YYYY',
                'verification_uri':'https://example.test/verify',
                'verification_uri_complete':'https://example.test/verify?user_code=ZZZZ-YYYY',
                'interval':3,
                'expires_in':600
            }).encode())
            return
        if self.path == '/token':
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
            return
        self.send_response(404)
        self.end_headers()

HTTPServer(('127.0.0.1', port), H).serve_forever()
PY
MOCK_PID=$!
trap 'kill "$MOCK_PID" >/dev/null 2>&1 || true' EXIT
sleep 0.2

START_URL="http://127.0.0.1:$PORT/device/code"
TOKEN_URL="http://127.0.0.1:$PORT/token"

# 1) native start-phase success when --device-code-json is omitted
READY=$("$BIN" models auth login --provider openai-codex --client-id stage17-client --scope "model.read model.write" --device-start-url "$START_URL" --config "$CFG")
echo "$READY" | grep -q '"phase": "device_code_ready"'
echo "$READY" | grep -q '"device_code": "dc-started"'

echo "$READY" >/tmp/nx-stage17-ready.json
python3 - <<'PY'
import json
j=json.load(open('/tmp/nx-stage17-ready.json'))
assert j['deviceCode']['verification_uri'] == 'https://example.test/verify'
assert j['deviceCode']['interval'] == 3
PY

# 2) native start-phase structured error
set +e
"$BIN" models auth login --provider openai-codex --client-id bad-client --device-start-url "$START_URL" --config "$CFG" >/tmp/nx-stage17-start-error.json 2>&1
RC=$?
set -e
if [[ "$RC" -eq 0 ]]; then
  echo "[FAIL] expected start error to fail"
  cat /tmp/nx-stage17-start-error.json
  exit 1
fi
grep -q '"phase": "device_code_start"' /tmp/nx-stage17-start-error.json
grep -q 'invalid_client' /tmp/nx-stage17-start-error.json

# 3) poll retryable error path (provided device-code-json still works)
set +e
"$BIN" models auth login --provider openai-codex --device-code-json '{"device_code":"pending"}' --poll --client-id stage17-client --token-url "$TOKEN_URL" --config "$CFG" >/tmp/nx-stage17-pending.json 2>&1
RC=$?
set -e
if [[ "$RC" -ne 2 ]]; then
  echo "[FAIL] expected retryable poll exit code 2, got $RC"
  cat /tmp/nx-stage17-pending.json
  exit 1
fi
grep -q '"retryable": true' /tmp/nx-stage17-pending.json

# 4) start->poll interoperability without --device-code-json
OK=$("$BIN" models auth login --provider openai-codex --poll --client-id stage17-client --scope "model.read" --device-start-url "$START_URL" --token-url "$TOKEN_URL" --profile-id stage17-native --config "$CFG")
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
