#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${POINTERVERSE_BIN:-$ROOT/build/pointerverse}"
STORE="${POINTERVERSE_PACK_STORE:-$SCRIPT_DIR/.pack-store}"

rm -rf "$STORE"
cd "$ROOT"

echo "== Rust wipe-day blackbox =="
"$BIN" repo init "$STORE"

echo
echo "== Baseline: the high-pop wipe pressure graph =="
"$BIN" repo --store "$STORE" run "$SCRIPT_DIR/world.pv" --branch main

# Each branch records one wipe as a graph-log. In observe mode the active laws do
# not block ingestion; they annotate every commit, so we can ask afterwards where
# the run first broke.
echo
echo "== Branch beach_rush: the fast, exposed wipe =="
"$BIN" repo --store "$STORE" branch fork main beach_rush
"$BIN" ingest graph-log "$SCRIPT_DIR/logs/beach_rush_events.jsonl" \
  --store "$STORE" --branch beach_rush --mode observe \
  --rules "$SCRIPT_DIR/rust_wipe_rules.pvdomain"

echo
echo "== Branch shadow_spoke: off-spine, banked progress =="
"$BIN" repo --store "$STORE" branch fork main shadow_spoke
"$BIN" ingest graph-log "$SCRIPT_DIR/logs/shadow_spoke_events.jsonl" \
  --store "$STORE" --branch shadow_spoke --mode observe \
  --rules "$SCRIPT_DIR/rust_wipe_rules.pvdomain"

echo
echo "== Branch panic_hop: leaving the server too early =="
"$BIN" repo --store "$STORE" branch fork main panic_hop
"$BIN" ingest graph-log "$SCRIPT_DIR/logs/panic_hop_events.jsonl" \
  --store "$STORE" --branch panic_hop --mode observe \
  --rules "$SCRIPT_DIR/rust_wipe_rules.pvdomain"

echo
echo "== Where did beach_rush first break each law? =="
"$BIN" audit first-broke beach_rush no_base_on_traffic_spine --store "$STORE"
"$BIN" audit first-broke beach_rush no_hot_base_before_exit_ready --store "$STORE"
"$BIN" audit first-broke beach_rush no_monument_before_depot_ready --store "$STORE"
"$BIN" audit first-broke beach_rush no_recycler_before_bag_triangle --store "$STORE"
"$BIN" audit first-broke beach_rush no_server_hop_before_failure_proven --store "$STORE"

echo
echo "== The wipe replayed as a timeline =="
"$BIN" audit timeline beach_rush SoloPlayer --store "$STORE"

echo
echo "== shadow_spoke broke nothing =="
"$BIN" audit first-broke shadow_spoke no_hot_base_before_exit_ready --store "$STORE"

echo
echo "== Pressure graph around the player =="
"$BIN" repo --store "$STORE" query-file shadow_spoke "$SCRIPT_DIR/queries/pressure.pvquery"

echo
echo "== Why is the shadow base off the spine? =="
"$BIN" repo --store "$STORE" why shadow_spoke ShadowRidgeSite avoids CoastRoad_East || true

echo
echo "== Where do the two wipes diverge? =="
"$BIN" repo --store "$STORE" branch compare beach_rush shadow_spoke || true

echo
echo "== Explain the commit where progress stabilized =="
STABLE_COMMIT="$("$BIN" repo --store "$STORE" history shadow_spoke | grep 'stabilizes' | awk '{print $1}' | head -1)"
if [[ -n "${STABLE_COMMIT}" ]]; then
  "$BIN" repo --store "$STORE" explain shadow_spoke commit "$STABLE_COMMIT"
else
  echo "no stabilized-progress commit found"
fi

echo
echo "== Backend and index status =="
"$BIN" repo --store "$STORE" stats
"$BIN" repo --store "$STORE" index check

echo
echo "== Sentinel verification =="
"$BIN" sentinel boot "$STORE"
"$BIN" sentinel patrol "$STORE" --once

echo
echo "== The whole store stays replayable =="
"$BIN" repo --store "$STORE" fsck
