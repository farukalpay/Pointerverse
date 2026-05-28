#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${POINTERVERSE_BIN:-$ROOT/build/pointerverse}"
OUT="${POINTERVERSE_PACK_OUT:-${TMPDIR:-/tmp}/pointerverse-code-review-pack}"

rm -rf "$OUT"
mkdir -p "$OUT"

"$BIN" guard run \
  --repo "$SCRIPT_DIR/after" \
  --base ../before \
  --mode observe \
  --format markdown \
  --out "$OUT/audit-report.md" \
  --store "$OUT/.pvstore"

cat "$OUT/audit-report.md"
