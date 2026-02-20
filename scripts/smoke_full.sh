#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

scripts/smoke_stage6.sh >/dev/null
scripts/smoke_models_cli.sh >/dev/null
scripts/smoke_stage10_cli.sh >/dev/null
scripts/smoke_stage11_models_auth.sh >/dev/null
scripts/smoke_stage11_installer.sh >/dev/null
scripts/smoke_stage12_gateway_security.sh >/dev/null
scripts/smoke_stage13_setup_wizard.sh >/dev/null
scripts/smoke_stage14_cron_semantics.sh >/dev/null
scripts/smoke_stage15_message_channels.sh >/dev/null
scripts/smoke_stage16_browser_oauth_message.sh >/dev/null
scripts/smoke_stage18_native_browser.sh >/dev/null
scripts/smoke_stage19_admin_dashboard.sh >/dev/null
scripts/smoke_stage21_agents.sh >/dev/null
scripts/smoke_stage22_nodes_canvas_devices.sh >/dev/null
scripts/smoke_stage23_control_plane_slice1.sh >/dev/null
scripts/smoke_stage25_nodes_runtime_slice2.sh >/dev/null
scripts/smoke_stage26_browser_act.sh >/dev/null
scripts/smoke_stage27_browser_act_kinds.sh >/dev/null
scripts/smoke_stage29_agents_orchestration.sh >/dev/null
scripts/smoke_stage30_browser_wait_native.sh >/dev/null
scripts/smoke_stage31_agents_advanced.sh >/dev/null
scripts/smoke_stage32_agents_context_tools.sh >/dev/null
scripts/smoke_stage33_agents_policy_enforcement.sh >/dev/null
scripts/smoke_stage34_agents_runtime_events.sh >/dev/null

scripts/smoke_stage35_agents_parity.sh >/dev/null
scripts/smoke_stage36_agents_parity.sh >/dev/null
scripts/smoke_stage37_agents_parity.sh >/dev/null
scripts/smoke_stage38_agents_parity.sh >/dev/null
scripts/smoke_stage39_agents_parity.sh >/dev/null
scripts/smoke_stage40_agents_parity.sh >/dev/null
scripts/smoke_stage41_agents_parity.sh >/dev/null
scripts/smoke_stage42_agents_parity.sh >/dev/null
scripts/smoke_stage43_agents_parity.sh >/dev/null
scripts/smoke_stage44_agents_parity.sh >/dev/null
scripts/smoke_stage45_agents_parity.sh >/dev/null
scripts/smoke_stage46_agents_parity.sh >/dev/null
scripts/smoke_stage47_agents_parity.sh >/dev/null
scripts/smoke_stage48_agents_parity.sh >/dev/null
scripts/smoke_stage49_agents_parity.sh >/dev/null
scripts/smoke_stage50_agents_parity.sh >/dev/null
scripts/smoke_stage51_agents_parity.sh >/dev/null
scripts/smoke_stage52_agents_parity.sh >/dev/null
scripts/smoke_stage53_agents_parity.sh >/dev/null
scripts/smoke_stage54_agents_parity.sh >/dev/null
scripts/smoke_stage55_agents_parity.sh >/dev/null
scripts/smoke_stage56_agents_parity.sh >/dev/null
scripts/smoke_stage57_agents_parity.sh >/dev/null
scripts/smoke_stage58_agents_parity.sh >/dev/null
scripts/smoke_stage59_agents_parity.sh >/dev/null
scripts/smoke_stage60_agents_parity.sh >/dev/null
scripts/smoke_stage61_agents_parity.sh >/dev/null
scripts/smoke_stage62_agents_parity.sh >/dev/null
scripts/smoke_stage63_agents_parity.sh >/dev/null
scripts/smoke_stage64_agents_parity.sh >/dev/null
scripts/smoke_stage65_agents_parity.sh >/dev/null
scripts/smoke_stage66_agents_parity.sh >/dev/null
scripts/smoke_stage67_agents_parity.sh >/dev/null
scripts/smoke_stage68_agents_parity.sh >/dev/null
scripts/smoke_stage69_agents_parity.sh >/dev/null
scripts/smoke_stage70_agents_parity.sh >/dev/null
scripts/smoke_stage71_agents_parity.sh >/dev/null
scripts/smoke_stage72_agents_parity.sh >/dev/null
scripts/smoke_stage73_agents_parity.sh >/dev/null
scripts/smoke_stage74_agents_parity.sh >/dev/null
scripts/smoke_stage75_agents_parity.sh >/dev/null
scripts/smoke_stage76_agents_parity.sh >/dev/null
scripts/smoke_stage77_agents_parity.sh >/dev/null
scripts/smoke_stage78_agents_parity.sh >/dev/null
scripts/smoke_stage79_agents_parity.sh >/dev/null
scripts/smoke_stage80_agents_parity.sh >/dev/null
scripts/smoke_stage81_agents_parity.sh >/dev/null
scripts/smoke_stage82_agents_parity.sh >/dev/null
scripts/smoke_stage83_agents_parity.sh >/dev/null
scripts/smoke_stage84_agents_parity.sh >/dev/null
scripts/smoke_stage85_agents_parity.sh >/dev/null
scripts/smoke_stage86_agents_parity.sh >/dev/null
scripts/smoke_stage87_agents_parity.sh >/dev/null
scripts/smoke_stage88_agents_parity.sh >/dev/null
scripts/smoke_stage89_agents_parity.sh >/dev/null
scripts/smoke_stage90_agents_parity.sh >/dev/null
scripts/smoke_stage91_agents_parity.sh >/dev/null
scripts/smoke_stage92_agents_parity.sh >/dev/null
scripts/smoke_stage93_agents_parity.sh >/dev/null
scripts/smoke_stage94_agents_parity.sh >/dev/null
scripts/smoke_stage95_agents_parity.sh >/dev/null
scripts/smoke_stage96_agents_parity.sh >/dev/null
scripts/smoke_stage97_agents_parity.sh >/dev/null
scripts/smoke_stage98_agents_parity.sh >/dev/null
scripts/smoke_stage99_agents_parity.sh >/dev/null
scripts/smoke_stage100_agents_parity.sh >/dev/null
scripts/smoke_stage101_agents_parity.sh >/dev/null
scripts/smoke_stage102_agents_parity.sh >/dev/null
scripts/smoke_stage103_agents_parity.sh >/dev/null
scripts/smoke_stage104_agents_parity.sh >/dev/null
scripts/smoke_stage105_agents_parity.sh >/dev/null
scripts/smoke_stage106_agents_parity.sh >/dev/null
scripts/smoke_stage107_agents_parity.sh >/dev/null
scripts/smoke_stage108_agents_parity.sh >/dev/null
scripts/smoke_stage109_agents_parity.sh >/dev/null
scripts/smoke_stage110_agents_parity.sh >/dev/null
scripts/smoke_stage111_agents_parity.sh >/dev/null
scripts/smoke_stage112_agents_parity.sh >/dev/null
scripts/smoke_stage113_agents_parity.sh >/dev/null
scripts/smoke_stage114_agents_parity.sh >/dev/null
scripts/smoke_stage115_agents_parity.sh >/dev/null
scripts/smoke_stage116_agents_parity.sh >/dev/null
scripts/smoke_stage117_agents_parity.sh >/dev/null
scripts/smoke_stage118_agents_parity.sh >/dev/null
scripts/smoke_stage119_agents_parity.sh >/dev/null
scripts/smoke_stage120_agents_parity.sh >/dev/null
BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

"$BIN" run --config config/config.json > /tmp/nexaclaw-full.log 2>&1 &
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
