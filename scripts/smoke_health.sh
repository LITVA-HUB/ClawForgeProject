#!/usr/bin/env bash
# Health endpoint smoke test.
# Verifies /health returns status:ok and (if version is present) non-empty version.
# Usage: scripts/smoke_health.sh [host:port]

set -euo pipefail

HOST="${1:-127.0.0.1:18890}"
BASE="http://$HOST"
PASS=0
FAIL=0

check_json_field() {
  local desc="$1" field="$2" expected="$3" json="$4"
  local actual
  actual=$(echo "$json" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('$field','<missing>'))" 2>/dev/null || echo "<parse-error>")
  if [ "$actual" = "$expected" ]; then
    echo "  PASS  $desc"
    PASS=$((PASS + 1))
  else
    echo "  FAIL  $desc [expected '$expected', got '$actual']"
    FAIL=$((FAIL + 1))
  fi
}

echo "=== Health smoke: $BASE ==="
echo ""

BODY=$(curl -sf "$BASE/health" 2>/dev/null || echo '{}')
HTTP=$(curl -s -o /dev/null -w "%{http_code}" "$BASE/health")

if [ "$HTTP" = "200" ]; then
  echo "  PASS  GET /health returns 200"
  PASS=$((PASS + 1))
else
  echo "  FAIL  GET /health returned $HTTP"
  FAIL=$((FAIL + 1))
fi

check_json_field "status == ok" "status" "ok" "$BODY"

# version field is optional (present after issue #9 is implemented)
VERSION=$(echo "$BODY" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d.get('version',''))" 2>/dev/null || echo "")
if [ -n "$VERSION" ]; then
  echo "  INFO  version: $VERSION"
fi

echo ""
if [ "$FAIL" -eq 0 ]; then
  echo "All $PASS checks passed."
else
  echo "$FAIL of $((PASS + FAIL)) checks failed."
  exit 1
fi
