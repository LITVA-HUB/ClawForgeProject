#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

CFG=/tmp/clawforge-smoke-stage11-config.json
STATE=/tmp/clawforge-smoke-stage11-state
cp config/config.example.json "$CFG"
python3 - <<'PY'
import json
p='/tmp/clawforge-smoke-stage11-config.json'
with open(p) as f:
    j=json.load(f)
j['stateDir']='/tmp/clawforge-smoke-stage11-state'
with open(p,'w') as f:
    json.dump(j,f,indent=2)
PY
rm -rf "$STATE"
mkdir -p "$STATE"

export OPENAI_API_KEY="env-openai-key-stage11"
export CF_STAGE11_TOKEN="oauth-token-stage11"

./build/clawforge models auth add --provider openai --profile-id smoke-openai --api-key-env OPENAI_API_KEY --config "$CFG" | grep -q '"ok": true'
./build/clawforge models auth use --provider openai --profile-id smoke-openai --config "$CFG" | grep -q '"ok": true'
./build/clawforge models auth paste-token --provider openai-codex --profile-id codex-smoke --token "$CF_STAGE11_TOKEN" --expires-in 3600 --config "$CFG" | grep -q '"ok": true'
./build/clawforge models auth setup-token --provider openai-codex --profile-id codex-manual --token "$CF_STAGE11_TOKEN" --expires-in 1200 --config "$CFG" | grep -q '"ok": true'
./build/clawforge models auth list --config "$CFG" | grep -q 'smoke-openai'
./build/clawforge models status --config "$CFG" | grep -q '"authSource": "profile"'
./build/clawforge models probe --config "$CFG" >/tmp/clawforge-stage11-probe.json 2>&1 || true
grep -q '"authSource"' /tmp/clawforge-stage11-probe.json
./build/clawforge models auth remove --profile-id codex-manual --config "$CFG" | grep -q '"ok": true'

echo "Smoke stage11 models auth: OK"
