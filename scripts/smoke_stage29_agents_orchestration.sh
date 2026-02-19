#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage29-config.json"
python3 - <<'PY' "$CFG"
import json,sys,uuid
cfg=json.load(open('config/config.example.json'))
base='/tmp/nexaclaw-smoke-stage29-' + uuid.uuid4().hex
cfg['stateDir']=base+'/state'
cfg['workspace']=base+'/workspace'
cfg['gateway']['auth']['mode']='off'
json.dump(cfg, open(sys.argv[1], 'w'), indent=2)
PY

"$BIN" agents create stage29-agent --name "Stage29 Agent" --config "$CFG" | grep -q '"created": true'
"$BIN" agents run stage29-agent --message "stage29 baseline run" --config "$CFG" | grep -Eq '"mode": "(gateway-task|gateway-message|local-session)"'
"$BIN" agents runs stage29-agent --limit 5 --config "$CFG" | grep -q '"runId": "run-'

BAD_THINK=$("$BIN" agents run stage29-agent --message "bad thinking" --thinking turbo --config "$CFG" || true)
echo "$BAD_THINK" | grep -q '"error": "invalid_thinking"'

ADVANCED_NO_GATEWAY=$("$BIN" agents run stage29-agent --message "advanced" --model gpt-5 --cleanup keep --config "$CFG" || true)
echo "$ADVANCED_NO_GATEWAY" | grep -q '"error": "advanced_options_require_gateway"'

"$BIN" agents history stage29-agent --limit 10 --config "$CFG" | grep -q '"subcommand": "runs"'

echo "Stage29 agents orchestration smoke: OK"
