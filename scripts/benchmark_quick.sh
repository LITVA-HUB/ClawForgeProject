#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="${BIN:-./build/nexaclaw}"
if [[ ! -x "$BIN" && -x ./build/clawforge ]]; then
  BIN="./build/clawforge"
fi

"$BIN" run --config config/config.json > /tmp/nexaclaw-bench.log 2>&1 &
PID=$!
trap 'kill $PID >/dev/null 2>&1 || true' EXIT
sleep 1

N=${1:-20}
START=$(python3 - <<'PY'
import time
print(time.time())
PY
)
for i in $(seq 1 "$N"); do
  curl -fsS -X POST http://127.0.0.1:18890/api/tasks \
    -H 'Content-Type: application/json' \
    -d '{"channel":"api","peerId":"bench","text":"/status"}' >/dev/null
done
END=$(python3 - <<'PY'
import time
print(time.time())
PY
)
python3 - <<PY
n=$N
start=float('$START')
end=float('$END')
sec=max(0.0001,end-start)
print(f'quick benchmark: {n} enqueue requests in {sec:.3f}s ({n/sec:.1f} req/s)')
PY
