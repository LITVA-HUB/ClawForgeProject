#!/usr/bin/env bash
set -eo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN="${BIN:-./build/clawforge}"
CONFIG_PATH="${CONFIG_PATH:-config/config.json}"
BASE_URL="${BASE_URL:-http://127.0.0.1:18890}"

if [[ ! -x "$BIN" ]]; then
  echo "[FAIL] binary not found: $BIN"
  exit 1
fi

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "[FAIL] config not found: $CONFIG_PATH"
  exit 1
fi

AUTH_MODE="$(python3 - <<'PY' "$CONFIG_PATH"
import json,sys
cfg=json.load(open(sys.argv[1]))
print(cfg.get('gateway',{}).get('auth',{}).get('mode','off'))
PY
)"
TOKEN_ENV="$(python3 - <<'PY' "$CONFIG_PATH"
import json,sys
cfg=json.load(open(sys.argv[1]))
print(cfg.get('gateway',{}).get('auth',{}).get('tokenEnv','CLAWFORGE_GATEWAY_TOKEN'))
PY
)"

declare -a AUTH_HEADER=()
if [[ "$AUTH_MODE" == "token" ]]; then
  TOKEN="${!TOKEN_ENV:-}"
  if [[ -z "$TOKEN" ]]; then
    echo "[FAIL] auth mode=token, env $TOKEN_ENV is empty"
    exit 1
  fi
  AUTH_HEADER=(-H "Authorization: Bearer $TOKEN")
fi

echo "[INFO] starting ClawForge..."
"$BIN" run --config "$CONFIG_PATH" > /tmp/clawforge-stage5.log 2>&1 &
PID=$!
trap 'kill $PID >/dev/null 2>&1 || true' EXIT
sleep 2

echo "[TEST] /health"
curl -fsS "$BASE_URL/health" >/dev/null
echo "[OK] /health"

echo "[TEST] /api/status unauthorized check"
if [[ "$AUTH_MODE" == "token" ]]; then
  code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/status")
  [[ "$code" == "401" ]] || { echo "[FAIL] expected 401, got $code"; exit 1; }
  echo "[OK] /api/status returns 401 without token"
else
  code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/status")
  [[ "$code" == "200" ]] || { echo "[FAIL] expected 200, got $code"; exit 1; }
  echo "[OK] /api/status returns 200 in auth=off mode"
fi

echo "[TEST] /api/status authorized"
code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/status" "${AUTH_HEADER[@]}")
[[ "$code" == "200" ]] || { echo "[FAIL] expected 200, got $code"; exit 1; }
echo "[OK] /api/status authorized"

echo "[TEST] SSE /api/events/stream"
SSE_OUT="/tmp/clawforge-stage5-sse.txt"
rm -f "$SSE_OUT"
curl -sN "$BASE_URL/api/events/stream" "${AUTH_HEADER[@]}" > "$SSE_OUT" &
SSE_PID=$!
sleep 1
curl -fsS -X POST "$BASE_URL/api/message" "${AUTH_HEADER[@]}" -H 'Content-Type: application/json' -d '{"sessionKey":"smoke-stage5","text":"ping sse"}' >/dev/null
sleep 2
kill "$SSE_PID" >/dev/null 2>&1 || true
wait "$SSE_PID" 2>/dev/null || true
if ! grep -q "event:" "$SSE_OUT"; then
  echo "[FAIL] SSE stream did not produce events"
  echo "--- sse ---"
  cat "$SSE_OUT" || true
  exit 1
fi
echo "[OK] SSE stream active"

echo "[TEST] cron run-now"
JOB_JSON=$(curl -fsS "$BASE_URL/api/cron/jobs" "${AUTH_HEADER[@]}")
JOB_ID=$(python3 - <<'PY' "$JOB_JSON"
import json,sys
data=json.loads(sys.argv[1])
jobs=data.get('jobs',[])
print(jobs[0]['id'] if jobs else '')
PY
)

if [[ -z "$JOB_ID" ]]; then
  echo "[INFO] no cron jobs, creating a temporary one"
  CREATE='{"name":"smoke-stage5","kind":"every","everyMs":3600000,"message":"smoke"}'
  CREATED=$(curl -fsS -X POST "$BASE_URL/api/cron/jobs" "${AUTH_HEADER[@]}" -H 'Content-Type: application/json' -d "$CREATE")
  JOB_ID=$(python3 - <<'PY' "$CREATED"
import json,sys
data=json.loads(sys.argv[1])
print(data.get('job',{}).get('id',''))
PY
)
fi

[[ -n "$JOB_ID" ]] || { echo "[FAIL] no cron job id"; exit 1; }
RUN=$(curl -fsS -X POST "$BASE_URL/api/cron/jobs/$JOB_ID/run-now" "${AUTH_HEADER[@]}")
python3 - <<'PY' "$RUN"
import json,sys
obj=json.loads(sys.argv[1])
assert obj.get('ok') is True, obj
PY

echo "[OK] cron run-now for job: $JOB_ID"
echo "[DONE] Stage 5 smoke passed"
