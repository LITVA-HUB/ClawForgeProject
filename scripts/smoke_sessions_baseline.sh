#!/usr/bin/env bash
# Sessions baseline smoke test.
# Usage: scripts/smoke_sessions_baseline.sh [host:port]

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

echo "=== Sessions baseline smoke: $BASE ==="

check "GET /api/sessions returns 200" 200 "$BASE/api/sessions"

BODY=$(curl -s "$BASE/api/sessions" 2>/dev/null || echo '{}')
if echo "$BODY" | python3 -c "import sys,json; json.load(sys.stdin)" 2>/dev/null; then
  echo "  PASS  /api/sessions response is valid JSON"; PASS=$((PASS+1))
else
  echo "  FAIL  /api/sessions response is not valid JSON"; FAIL=$((FAIL+1))
fi

echo ""
[ "$FAIL" -eq 0 ] && echo "All $PASS checks passed." || { echo "$FAIL/$((PASS+FAIL)) failed."; exit 1; }
