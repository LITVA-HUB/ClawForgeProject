#!/usr/bin/env bash
# Cron baseline smoke test — verifies cron list/status/validate endpoints.
# Usage: scripts/smoke_cron_baseline.sh [host:port]

set -euo pipefail
HOST="${1:-127.0.0.1:18890}"
BASE="http://$HOST"
PASS=0; FAIL=0

check() {
  local desc="$1" expected_code="$2"; shift 2
  local code
  code=$(curl -s -o /dev/null -w "%{http_code}" "$@")
  if [ "$code" = "$expected_code" ]; then
    echo "  PASS  [$code] $desc"; PASS=$((PASS+1))
  else
    echo "  FAIL  [$expected_code expected, got $code] $desc"; FAIL=$((FAIL+1))
  fi
}

check_json() {
  local desc="$1" field="$2" expected="$3" json="$4"
  local actual
  actual=$(echo "$json" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('$field','<missing>'))" 2>/dev/null || echo "<err>")
  if [ "$actual" = "$expected" ]; then
    echo "  PASS  $desc"; PASS=$((PASS+1))
  else
    echo "  FAIL  $desc [expected '$expected', got '$actual']"; FAIL=$((FAIL+1))
  fi
}

echo "=== Cron baseline smoke: $BASE ==="

check "GET /api/cron/list returns 200"   200 "$BASE/api/cron/list"
check "GET /api/cron/status returns 200" 200 "$BASE/api/cron/status"

VALIDATE=$(curl -s -X POST "$BASE/api/cron/validate" \
  -H 'Content-Type: application/json' \
  -d '{"schedule":"0 9 * * *"}' 2>/dev/null || echo '{}')
check_json "validate '0 9 * * *' returns ok" "ok" "true" "$VALIDATE"

echo ""
[ "$FAIL" -eq 0 ] && echo "All $PASS checks passed." || { echo "$FAIL/$((PASS+FAIL)) failed."; exit 1; }
