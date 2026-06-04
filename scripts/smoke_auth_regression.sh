#!/usr/bin/env bash
# Auth token regression smoke test.
# Requires a running nexaclaw with auth.mode=token.
# Usage: NEXACLAW_GATEWAY_TOKEN=<token> scripts/smoke_auth_regression.sh [host:port]

set -euo pipefail

HOST="${1:-127.0.0.1:18890}"
BASE="http://$HOST"
TOKEN="${NEXACLAW_GATEWAY_TOKEN:-}"
PASS=0
FAIL=0

check() {
  local desc="$1" expected="$2"
  shift 2
  local actual
  actual=$(curl -s -o /dev/null -w "%{http_code}" "$@")
  if [ "$actual" = "$expected" ]; then
    echo "  PASS  [$expected] $desc"
    PASS=$((PASS + 1))
  else
    echo "  FAIL  [$expected expected, got $actual] $desc"
    FAIL=$((FAIL + 1))
  fi
}

if [ -z "$TOKEN" ]; then
  echo "SKIP: NEXACLAW_GATEWAY_TOKEN is not set — cannot run auth regression tests"
  exit 0
fi

echo "=== Auth token regression: $BASE ==="
echo ""

echo "-- Should return 401 (no auth header) --"
check "no Authorization header"         401 "$BASE/api/status"
check "empty Authorization header"      401 -H "Authorization: " "$BASE/api/status"
check "wrong scheme (Basic)"            401 -H "Authorization: Basic $TOKEN" "$BASE/api/status"
check "lowercase bearer scheme"         401 -H "Authorization: bearer $TOKEN" "$BASE/api/status"
check "bearer with empty token"         401 -H "Authorization: Bearer " "$BASE/api/status"
check "bearer with whitespace token"    401 -H "Authorization: Bearer   " "$BASE/api/status"

echo ""
echo "-- Should return 200 (valid token) --"
check "valid Bearer token"              200 -H "Authorization: Bearer $TOKEN" "$BASE/api/status"

echo ""
if [ "$FAIL" -eq 0 ]; then
  echo "All $PASS checks passed."
else
  echo "$FAIL of $((PASS + FAIL)) checks failed."
  exit 1
fi
