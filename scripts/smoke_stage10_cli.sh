#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."
CFG="config/config.json"

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

"$BIN" browser status --config "$CFG" | grep -q '"ok"'
"$BIN" browser snapshot --config "$CFG" >/tmp/nexaclaw-browser-snapshot.json 2>&1 || true
grep -q '"implemented"' /tmp/nexaclaw-browser-snapshot.json

"$BIN" cron validate --json '{"name":"smoke","kind":"every","everyMs":120000,"sessionKey":"main","message":"/status"}' --config "$CFG" | grep -q '"ok": true'

"$BIN" tools call exec --json '{"command":"echo stage10"}' --config "$CFG" | grep -q '"ok": true'

"$BIN" config set api.dmScope main --config "$CFG" >/dev/null
"$BIN" config get api.dmScope --config "$CFG" | grep -q '^main$'
"$BIN" config set telegram.dmPolicy open --config "$CFG" >/dev/null
"$BIN" config get telegram.dmPolicy --config "$CFG" | grep -q '^open$'
"$BIN" config set models.routing.image openai/gpt-image-1 --config "$CFG" >/dev/null
"$BIN" config get models.routing.image --config "$CFG" | grep -q 'openai/gpt-image-1'

"$BIN" models probe --config "$CFG" >/tmp/nexaclaw-models-probe.json 2>&1 || true
grep -q '"checks"' /tmp/nexaclaw-models-probe.json
"$BIN" models set-image openai/gpt-image-1 --config "$CFG" | grep -q 'Image model set'
"$BIN" image-fallbacks clear --config "$CFG" >/dev/null
"$BIN" image-fallbacks add openrouter/stability/sdxl --config "$CFG" >/dev/null
"$BIN" image-fallbacks list --config "$CFG" | grep -q 'openrouter/stability/sdxl'
"$BIN" image-fallbacks remove openrouter/stability/sdxl --config "$CFG" >/dev/null

"$BIN" logs tail 5 --config "$CFG" >/dev/null || true
"$BIN" system event "stage10 smoke event" --config "$CFG" | grep -q '"ok": true'

echo "Stage10 CLI smoke: OK"
