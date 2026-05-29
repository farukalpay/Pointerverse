#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${POINTERVERSE_BIN:-$ROOT/build/pointerverse}"
STORE="${POINTERVERSE_PACK_STORE:-$SCRIPT_DIR/.pack-store}"

rm -rf "$STORE"

cd "$ROOT"

# Baseline: both armies arrayed at Mohács, the relief armies still on the road.
"$BIN" repo init "$STORE"
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/world.pv" --branch main

# The historical line gives battle early. The active law flags that decision, so
# this run reports an expected rejection before the afternoon plays out.
"$BIN" repo --store "$STORE" branch fork main historical
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/historical.pv" --branch historical || true

# The counterfactual waits for the relief armies; the same law then passes.
"$BIN" repo --store "$STORE" branch fork main reinforced
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/reinforced.pv" --branch reinforced

# Read the two histories back out of the store.
"$BIN" repo --store "$STORE" history historical
"$BIN" repo --store "$STORE" query historical objects type Army
"$BIN" repo --store "$STORE" why historical OttomanArmy defeats Louis_II

# Name where the two afternoons first diverge.
"$BIN" repo --store "$STORE" branch compare historical reinforced || true

# Explain the commit where the counterfactual gives battle: exact delta + law.
GIVES_BATTLE="$("$BIN" repo --store "$STORE" history reinforced | grep 'gives_battle' | awk '{print $1}' | head -1)"
"$BIN" repo --store "$STORE" explain reinforced commit "$GIVES_BATTLE"

# The whole store stays replayable and intact.
"$BIN" repo --store "$STORE" fsck
