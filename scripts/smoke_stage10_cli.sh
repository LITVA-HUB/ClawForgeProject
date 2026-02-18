#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
CFG="config/config.json"

./build/clawforge browser status --config "$CFG" | grep -q '"ok"'
./build/clawforge browser snapshot --config "$CFG" >/tmp/clawforge-browser-snapshot.json 2>&1 || true
grep -q '"implemented"' /tmp/clawforge-browser-snapshot.json

./build/clawforge cron validate --json '{"name":"smoke","kind":"every","everyMs":120000,"sessionKey":"main","message":"/status"}' --config "$CFG" | grep -q '"ok": true'

./build/clawforge tools call exec --json '{"command":"echo stage10"}' --config "$CFG" | grep -q '"ok": true'

./build/clawforge config set api.dmScope main --config "$CFG" >/dev/null
./build/clawforge config get api.dmScope --config "$CFG" | grep -q '^main$'
./build/clawforge config set telegram.dmPolicy open --config "$CFG" >/dev/null
./build/clawforge config get telegram.dmPolicy --config "$CFG" | grep -q '^open$'
./build/clawforge config set models.routing.image openai/gpt-image-1 --config "$CFG" >/dev/null
./build/clawforge config get models.routing.image --config "$CFG" | grep -q 'openai/gpt-image-1'

./build/clawforge models probe --config "$CFG" >/tmp/clawforge-models-probe.json 2>&1 || true
grep -q '"checks"' /tmp/clawforge-models-probe.json
./build/clawforge models set-image openai/gpt-image-1 --config "$CFG" | grep -q 'Image model set'
./build/clawforge image-fallbacks clear --config "$CFG" >/dev/null
./build/clawforge image-fallbacks add openrouter/stability/sdxl --config "$CFG" >/dev/null
./build/clawforge image-fallbacks list --config "$CFG" | grep -q 'openrouter/stability/sdxl'
./build/clawforge image-fallbacks remove openrouter/stability/sdxl --config "$CFG" >/dev/null

./build/clawforge logs tail 5 --config "$CFG" >/dev/null || true
./build/clawforge system event "stage10 smoke event" --config "$CFG" | grep -q '"ok": true'

echo "Stage10 CLI smoke: OK"
