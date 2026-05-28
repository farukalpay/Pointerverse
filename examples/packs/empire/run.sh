#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${POINTERVERSE_BIN:-$ROOT/build/pointerverse}"
STORE="${POINTERVERSE_PACK_STORE:-$SCRIPT_DIR/.pack-store}"

rm -rf "$STORE"

cd "$ROOT"

"$BIN" repo init "$STORE"
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/world.pv" --branch main

"$BIN" repo --store "$STORE" branch fork main plague
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/plague_branch.pv" --branch plague

"$BIN" repo --store "$STORE" branch fork main rebellion
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/rebellion_branch.pv" --branch rebellion

"$BIN" repo --store "$STORE" branch fork main succession
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/succession_branch.pv" --branch succession

"$BIN" repo --store "$STORE" history plague
"$BIN" repo --store "$STORE" query plague objects type Plague
"$BIN" repo --store "$STORE" why plague QuarantineEdict quarantines Harbor
"$BIN" repo --store "$STORE" branch compare plague rebellion || true
"$BIN" repo --store "$STORE" fsck
