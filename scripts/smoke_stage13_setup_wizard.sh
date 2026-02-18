#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG=/tmp/nexaclaw-smoke-stage13-config.json
cp config/config.example.json "$CFG"

"$BIN" setup --non-interactive --config "$CFG" | grep -q '"ok": true'
"$BIN" onboard --non-interactive --config "$CFG" | grep -q '"ok": true'
"$BIN" configure --non-interactive --config "$CFG" | grep -q '"ok": true'

"$BIN" config get api.dmScope --config "$CFG" | grep -q '^per-channel-peer$'
"$BIN" config get gateway.auth.tokenEnv --config "$CFG" | grep -q '^NEXACLAW_GATEWAY_TOKEN$'

echo "Stage13 setup wizard smoke: OK"
