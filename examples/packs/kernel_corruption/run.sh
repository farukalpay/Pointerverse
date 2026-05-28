#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${POINTERVERSE_BIN:-$ROOT/build/pointerverse}"
STORE="${POINTERVERSE_PACK_STORE:-${TMPDIR:-/tmp}/pointerverse-kernel-corruption-pack.pvstore}"

rm -rf "$STORE"

"$BIN" repo init "$STORE"
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/world.pv" --branch main
"$BIN" sentinel boot "$STORE"
"$BIN" sentinel fault flip-proof "$STORE" --branch main --commit HEAD --yes-i-know-this-mutates-store

if "$BIN" sentinel boot "$STORE"; then
  echo "sentinel failed to detect injected proof mismatch" >&2
  exit 1
fi

echo "kernel corruption pack detected the injected proof mismatch"
