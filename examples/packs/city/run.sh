#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${POINTERVERSE_BIN:-$ROOT/build/pointerverse}"
STORE="${POINTERVERSE_PACK_STORE:-${TMPDIR:-/tmp}/pointerverse-city-pack.pvstore}"

rm -rf "$STORE"

"$BIN" repo init "$STORE"
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/world.pv" --branch main

"$BIN" repo --store "$STORE" branch fork main flood
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/flood.pv" --branch flood

"$BIN" repo --store "$STORE" branch fork main blackout
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/blackout.pv" --branch blackout

"$BIN" repo --store "$STORE" branch fork main evacuation
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/evacuation.pv" --branch evacuation

"$BIN" repo --store "$STORE" history blackout
"$BIN" repo --store "$STORE" query blackout objects type Infrastructure
"$BIN" repo --store "$STORE" why blackout Hospital depends_on PowerPlant
"$BIN" repo --store "$STORE" branch compare flood blackout || true
"$BIN" sentinel boot "$STORE"
"$BIN" repo --store "$STORE" fsck
