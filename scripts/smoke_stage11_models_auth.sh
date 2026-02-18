#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

CFG=/tmp/nexaclaw-smoke-stage11-config.json
STATE=/tmp/nexaclaw-smoke-stage11-state
cp config/config.example.json "$CFG"
python3 - <<'PY'
import json
p='/tmp/nexaclaw-smoke-stage11-config.json'
with open(p) as f:
    j=json.load(f)
j['stateDir']='/tmp/nexaclaw-smoke-stage11-state'
with open(p,'w') as f:
    json.dump(j,f,indent=2)
PY
rm -rf "$STATE"
mkdir -p "$STATE"

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

export OPENAI_API_KEY="env-openai-key-stage11"
export NX_STAGE11_TOKEN="oauth-token-stage11"

"$BIN" models auth add --provider openai --profile-id smoke-openai --api-key-env OPENAI_API_KEY --config "$CFG" | grep -q '"ok": true'
"$BIN" models auth use --provider openai --profile-id smoke-openai --config "$CFG" | grep -q '"ok": true'
"$BIN" models auth paste-token --provider openai-codex --profile-id codex-smoke --token "$NX_STAGE11_TOKEN" --expires-in 3600 --config "$CFG" | grep -q '"ok": true'
"$BIN" models auth setup-token --provider openai-codex --profile-id codex-manual --token "$NX_STAGE11_TOKEN" --expires-in 1200 --config "$CFG" | grep -q '"ok": true'
"$BIN" models auth list --config "$CFG" | grep -q 'smoke-openai'
"$BIN" models status --config "$CFG" | grep -q '"authSource": "profile"'
"$BIN" models probe --config "$CFG" >/tmp/nexaclaw-stage11-probe.json 2>&1 || true
grep -q '"authSource"' /tmp/nexaclaw-stage11-probe.json
"$BIN" models auth remove --profile-id codex-manual --config "$CFG" | grep -q '"ok": true'

echo "Smoke stage11 models auth: OK"
