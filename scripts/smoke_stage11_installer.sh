#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

OUT=/tmp/nexaclaw-installer-dryrun.log
bash scripts/install.sh --dry-run --repo https://github.com/LITVA-HUB/ClawForgeProject.git --branch main --dir /tmp/nexaclaw-install-smoke --bin-dir /tmp/nexaclaw-bin-smoke >"$OUT"

grep -q "\[dry-run\] git clone\|\[dry-run\] git -C" "$OUT"
grep -q "\[dry-run\] cmake -S" "$OUT"
grep -q "\[dry-run\] cmake --build" "$OUT"
grep -q "nexaclaw" "$OUT"
grep -q "Compatibility alias" "$OUT"

bash scripts/install.sh --validate >/tmp/nexaclaw-installer-validate.log

echo "Smoke stage11 installer dry-run: OK"
