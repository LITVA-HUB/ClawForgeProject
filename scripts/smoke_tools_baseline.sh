#!/usr/bin/env bash
# Tools baseline smoke test.
# Usage: scripts/smoke_tools_baseline.sh [host:port]

set -euo pipefail
HOST="${1:-127.0.0.1:18890}"
BASE="http://$HOST"
PASS=0; FAIL=0

check() {
  local desc="$1" expected="$2"; shift 2
  local code
  code=$(curl -s -o /dev/null -w "%{http_code}" "$@")
  if [ "$code" = "$expected" ]; then
    echo "  PASS  [$code] $desc"; PASS=$((PASS+1))
  else
    echo "  FAIL  [$expected expected, got $code] $desc"; FAIL=$((FAIL+1))
  fi
}

echo "=== Tools baseline smoke: $BASE ==="

check "GET /api/tools returns 200" 200 "$BASE/api/tools"

BODY=$(curl -s "$BASE/api/tools" 2>/dev/null || echo 'null')
if echo "$BODY" | python3 -c "import sys,json; d=json.load(sys.stdin); assert isinstance(d, (list, dict))" 2>/dev/null; then
  echo "  PASS  /api/tools returns list or object"; PASS=$((PASS+1))
else
  echo "  FAIL  /api/tools did not return list or object"; FAIL=$((FAIL+1))
fi

# Call unknown tool — should return 404 or 400, not 500
code=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$BASE/api/tools/__nonexistent__" \
  -H 'Content-Type: application/json' -d '{}')
if [ "$code" = "404" ] || [ "$code" = "400" ]; then
  echo "  PASS  unknown tool returns $code (not 500)"; PASS=$((PASS+1))
else
  echo "  FAIL  unknown tool returned $code (expected 404 or 400)"; FAIL=$((FAIL+1))
fi

echo ""
[ "$FAIL" -eq 0 ] && echo "All $PASS checks passed." || { echo "$FAIL/$((PASS+FAIL)) failed."; exit 1; }
