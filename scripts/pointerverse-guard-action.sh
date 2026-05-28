#!/usr/bin/env bash
set -euo pipefail

ACTION_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="${GITHUB_WORKSPACE:-$PWD}"
REPO_INPUT="${POINTERVERSE_GUARD_REPO:-.}"
BASE="${POINTERVERSE_GUARD_BASE:-origin/main}"
MODE="${POINTERVERSE_GUARD_MODE:-observe}"
REPORT="${POINTERVERSE_GUARD_REPORT:-audit-report.md}"
FORMAT="${POINTERVERSE_GUARD_FORMAT:-markdown}"
SARIF="${POINTERVERSE_GUARD_SARIF:-audit.sarif}"

if [[ "$REPO_INPUT" = /* ]]; then
  TARGET_REPO="$REPO_INPUT"
else
  TARGET_REPO="$WORKSPACE/$REPO_INPUT"
fi

BINARY="${POINTERVERSE_BIN:-}"
if [[ -z "$BINARY" ]]; then
  if [[ -x "$ACTION_ROOT/build/pointerverse" ]]; then
    BINARY="$ACTION_ROOT/build/pointerverse"
  elif [[ -x "$ACTION_ROOT/build/apps/pointerverse/pointerverse" ]]; then
    BINARY="$ACTION_ROOT/build/apps/pointerverse/pointerverse"
  else
    BUILD_DIR="${RUNNER_TEMP:-/tmp}/pointerverse-guard-build"
    if [[ -z "${VCPKG_ROOT:-}" ]] && command -v apt-get >/dev/null 2>&1 && command -v sudo >/dev/null 2>&1; then
      sudo apt-get update
      sudo apt-get install -y cmake g++ ninja-build libfmt-dev nlohmann-json3-dev libcli11-dev libssl-dev
    fi
    if [[ -n "${VCPKG_ROOT:-}" ]]; then
      cmake -S "$ACTION_ROOT" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DPOINTERVERSE_BUILD_TESTS=OFF
    else
      cmake -S "$ACTION_ROOT" -B "$BUILD_DIR" -G Ninja -DPOINTERVERSE_BUILD_TESTS=OFF
    fi
    cmake --build "$BUILD_DIR" --target pointerverse
    if [[ -x "$BUILD_DIR/pointerverse" ]]; then
      BINARY="$BUILD_DIR/pointerverse"
    else
      BINARY="$BUILD_DIR/apps/pointerverse/pointerverse"
    fi
  fi
fi

"$BINARY" guard run \
  --repo "$TARGET_REPO" \
  --base "$BASE" \
  --mode "$MODE" \
  --format "$FORMAT" \
  --out "$WORKSPACE/$REPORT"

if [[ -n "$SARIF" ]]; then
  "$BINARY" guard run \
    --repo "$TARGET_REPO" \
    --base "$BASE" \
    --mode observe \
    --format sarif \
    --out "$WORKSPACE/$SARIF"
fi
