#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

OUT=/tmp/clawforge-installer-dryrun.log
bash scripts/install.sh --dry-run --repo https://github.com/LITVA-HUB/ClawForgeProject.git --branch main --dir /tmp/clawforge-install-smoke --bin-dir /tmp/clawforge-bin-smoke >"$OUT"

grep -q "\[dry-run\] git clone\|\[dry-run\] git -C" "$OUT"
grep -q "\[dry-run\] cmake -S" "$OUT"
grep -q "\[dry-run\] cmake --build" "$OUT"
grep -q "Done. Binary:" "$OUT"

OUTV=/tmp/clawforge-installer-validate.log
bash scripts/install.sh --validate >"$OUTV"
grep -q "Validation OK" "$OUTV"
grep -q "Validate-only mode: no changes applied" "$OUTV"

echo "Smoke stage11 installer dry-run/validate: OK"
