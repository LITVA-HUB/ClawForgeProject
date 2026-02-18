#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

if [[ ! -f config/config.json ]]; then
  cp config/config.example.json config/config.json
  echo "Created config/config.json from example"
fi

cmake -S . -B build
cmake --build build -j

CLI="./build/nexaclaw"
if [[ ! -x "$CLI" && -x ./build/clawforge ]]; then
  CLI="./build/clawforge"
fi

"$CLI" --doctor --config config/config.json || true

echo "Bootstrap complete."
echo "Next: scripts/smoke_full.sh"
echo "Run service: $CLI run --config config/config.json"
