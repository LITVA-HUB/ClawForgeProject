#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

CFG=/tmp/nexaclaw-smoke-models-config.json
cp config/config.example.json "$CFG"

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

"$BIN" models list --config "$CFG" | grep -q '"models"'
"$BIN" models status --config "$CFG" | grep -q '"providers"'
"$BIN" models aliases add testalias openai/gpt-4o-mini --config "$CFG" | grep -q OK
"$BIN" models aliases list --config "$CFG" | grep -q 'testalias'
"$BIN" models set testalias --config "$CFG" | grep -q 'Current model set'
"$BIN" models fallbacks add anthropic/claude-3-5-haiku-latest --config "$CFG" | grep -q OK
"$BIN" models fallbacks list --config "$CFG" | grep -q 'anthropic/claude-3-5-haiku-latest'
"$BIN" config get model.current --config "$CFG" | grep -q testalias
"$BIN" config set model.current openai/gpt-4o-mini --config "$CFG" | grep -q OK
"$BIN" config get model.current --config "$CFG" | grep -q 'openai/gpt-4o-mini'

echo "Smoke models CLI: OK"
