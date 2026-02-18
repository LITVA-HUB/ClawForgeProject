#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build >/dev/null
cmake --build build -j >/dev/null

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

"$BIN" run --config config/config.json > /tmp/nexaclaw-stage6.log 2>&1 &
PID=$!
trap 'kill $PID >/dev/null 2>&1 || true' EXIT
sleep 1

curl -fsS http://127.0.0.1:18890/health >/dev/null
curl -fsS http://127.0.0.1:18890/api/browser/status | grep -q '"ok": true'
curl -fsS -X POST http://127.0.0.1:18890/api/browser/open -H 'Content-Type: application/json' -d '{"url":"https://example.com"}' | grep -q '"ok": true'
curl -sS -X POST http://127.0.0.1:18890/api/browser/snapshot -H 'Content-Type: application/json' -d '{"url":"https://example.com"}' | grep -q '"implemented": false'

# policy check via scoped channel deny (telegram denies exec in sample config)
RES=$(curl -sS -X POST http://127.0.0.1:18890/api/tools/exec -H 'Content-Type: application/json' -d '{"channel":"telegram","peerId":"123","command":"echo hi"}')
echo "$RES" | grep -q '"ok": false'
echo "$RES" | grep -q 'policyReason'

echo "Stage6 smoke: OK"
