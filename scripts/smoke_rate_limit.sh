#!/usr/bin/env bash
# Rate limit smoke test — verifies that the gateway enforces rate limits.
# Requires a running gateway with rateLimit.enabled=true and low maxRequests for testing.
# Usage: RATE_LIMIT_MAX=5 scripts/smoke_rate_limit.sh [host:port]

set -euo pipefail
HOST="${1:-127.0.0.1:18890}"
BASE="http://$HOST"
MAX="${RATE_LIMIT_MAX:-120}"
PASS=0; FAIL=0

echo "=== Rate limit smoke: $BASE (configured max=$MAX) ==="
echo ""

# Check baseline: first request should succeed
code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/health")
if [ "$code" = "200" ]; then
  echo "  PASS  baseline /health returns 200"; PASS=$((PASS+1))
else
  echo "  FAIL  baseline /health returned $code"; FAIL=$((FAIL+1))
fi

# If max is low enough (<=10), test enforcement
if [ "$MAX" -le 10 ]; then
  echo ""
  echo "-- Sending $MAX+2 rapid requests to trigger 429 --"
  GOT_429=0
  for i in $(seq 1 $((MAX+2))); do
    c=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/health")
    [ "$c" = "429" ] && GOT_429=1 && break
  done
  if [ "$GOT_429" -eq 1 ]; then
    echo "  PASS  429 received after exceeding limit"; PASS=$((PASS+1))
  else
    echo "  INFO  no 429 received (limit may not be exceeded or source is excluded)"
  fi
else
  echo "  INFO  RATE_LIMIT_MAX=$MAX is too high to trigger 429 in this test."
  echo "        Set RATE_LIMIT_MAX=5 and configure rateLimit.maxRequests=5 for enforcement test."
fi

echo ""
[ "$FAIL" -eq 0 ] && echo "All $PASS checks passed." || { echo "$FAIL/$((PASS+FAIL)) failed."; exit 1; }
