#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${POINTERVERSE_BIN:-$ROOT/build/pointerverse}"
STORE="${POINTERVERSE_SENTINEL_DEMO_STORE:-${TMPDIR:-/tmp}/pointerverse-sentinel-fault-demo.pvstore}"

rm -rf "$STORE"

"$BIN" repo init "$STORE"
"$BIN" repo --store "$STORE" run "$ROOT/examples/minimal_reality.pv" --branch main
"$BIN" sentinel boot "$STORE"
"$BIN" sentinel fault flip-proof "$STORE" --branch main --commit HEAD --yes-i-know-this-mutates-store

if "$BIN" sentinel boot "$STORE"; then
  echo "sentinel failed to detect injected proof mismatch" >&2
  exit 1
fi

echo "sentinel fault demo detected the injected proof mismatch"
