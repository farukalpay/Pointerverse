#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${POINTERVERSE_BIN:-$ROOT/build/pointerverse}"
STORE="${POINTERVERSE_PACK_STORE:-${TMPDIR:-/tmp}/pointerverse-build-system-pack.pvstore}"

rm -rf "$STORE"
"$BIN" repo init "$STORE"
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/world.pv" --branch main
"$BIN" repo --store "$STORE" query main objects type Artifact
"$BIN" repo --store "$STORE" fsck
