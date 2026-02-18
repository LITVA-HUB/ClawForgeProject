#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

CFG=/tmp/clawforge-smoke-models-config.json
cp config/config.example.json "$CFG"

./build/clawforge models list --config "$CFG" | grep -q '"models"'
./build/clawforge models status --config "$CFG" | grep -q '"providers"'
./build/clawforge models aliases add testalias openai/gpt-4o-mini --config "$CFG" | grep -q OK
./build/clawforge models aliases list --config "$CFG" | grep -q 'testalias'
./build/clawforge models set testalias --config "$CFG" | grep -q 'Current model set'
./build/clawforge models fallbacks add anthropic/claude-3-5-haiku-latest --config "$CFG" | grep -q OK
./build/clawforge models fallbacks list --config "$CFG" | grep -q 'anthropic/claude-3-5-haiku-latest'
./build/clawforge config get model.current --config "$CFG" | grep -q testalias
./build/clawforge config set model.current openai/gpt-4o-mini --config "$CFG" | grep -q OK
./build/clawforge config get model.current --config "$CFG" | grep -q 'openai/gpt-4o-mini'

echo "Smoke models CLI: OK"
