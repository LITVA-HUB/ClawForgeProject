#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

CFG="/tmp/nexaclaw-smoke-stage15-config.json"
cp config/config.example.json "$CFG"

"$BIN" channels list --config "$CFG" | grep -q '"channel": "telegram"'
"$BIN" channels add --channel telegram --dm-policy pairing --config "$CFG" | grep -q '"ok": true'
"$BIN" channels status --config "$CFG" | grep -q '"enabled": true'

# explicit channel
"$BIN" message send --channel telegram --target @example_user --message "stage15 hello" --dry-run --config "$CFG" | grep -q '"dryRun": true'

# default channel auto-detection (exactly one enabled)
"$BIN" message send --target @example_user --message "stage15 auto-channel" --dry-run --config "$CFG" | grep -q '"channel": "telegram"'

if "$BIN" message send --channel telegram --target bad-target --message "oops" --dry-run --config "$CFG" >/tmp/nexaclaw-stage15-invalid-target.json 2>&1; then
  echo "[FAIL] invalid message target unexpectedly accepted"
  exit 1
fi
grep -q 'telegram target must be @username' /tmp/nexaclaw-stage15-invalid-target.json

"$BIN" channels remove --channel telegram --config "$CFG" | grep -q '"enabled": false'

echo "Stage15 message/channels smoke: OK"
