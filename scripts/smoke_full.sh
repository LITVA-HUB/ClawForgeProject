#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

scripts/smoke_stage6.sh >/dev/null
scripts/smoke_models_cli.sh >/dev/null
scripts/smoke_stage10_cli.sh >/dev/null
scripts/smoke_stage11_models_auth.sh >/dev/null
scripts/smoke_stage11_installer.sh >/dev/null
scripts/smoke_stage12_gateway_security.sh >/dev/null

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

"$BIN" run --config config/config.json > /tmp/nexaclaw-full.log 2>&1 &
PID=$!
trap 'kill $PID >/dev/null 2>&1 || true' EXIT
sleep 1

TASK=$(curl -fsS -X POST http://127.0.0.1:18890/api/tasks -H 'Content-Type: application/json' -d '{"channel":"api","peerId":"smoke","text":"/status","timeoutMs":5000}')
echo "$TASK" | grep -q '"ok": true'
ID=$(echo "$TASK" | python3 -c 'import json,sys;print(json.load(sys.stdin)["task"]["id"])')
sleep 1
curl -fsS "http://127.0.0.1:18890/api/tasks/$ID" | grep -Eq '"status": "(done|failed|timeout|cancelled)"'

curl -fsS http://127.0.0.1:18890/api/tasks | grep -q '"tasks"'

echo "Full smoke: OK"
