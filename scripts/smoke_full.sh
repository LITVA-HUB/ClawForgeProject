#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

scripts/smoke_stage6.sh >/dev/null
scripts/smoke_models_cli.sh >/dev/null

./build/clawforge run --config config/config.json > /tmp/clawforge-full.log 2>&1 &
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
