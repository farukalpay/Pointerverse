#!/usr/bin/env bash
set -euo pipefail

APP_USER="${POINTERVERSE_USER:-pointerverse}"
BASE_DIR="${POINTERVERSE_BASE_DIR:-/opt/pointerverse}"
REPO_DIR="${POINTERVERSE_REPO_DIR:-$BASE_DIR/repo}"
REPO_URL="${POINTERVERSE_REPO_URL:-https://github.com/farukalpay/Pointerverse.git}"
BRANCH="${POINTERVERSE_BRANCH:-main}"
VCPKG_ROOT="${VCPKG_ROOT:-$BASE_DIR/vcpkg}"
PORT="${POINTERVERSE_PORT:-80}"

as_app() {
  if [[ "$(id -u)" == "0" ]]; then
    runuser -u "$APP_USER" -- "$@"
  else
    "$@"
  fi
}

ensure_user_and_dirs() {
  if [[ "$(id -u)" == "0" ]] && ! id "$APP_USER" >/dev/null 2>&1; then
    useradd --system --home "$BASE_DIR" --shell /usr/sbin/nologin "$APP_USER"
  fi
  mkdir -p "$BASE_DIR"
  if [[ "$(id -u)" == "0" ]]; then
    chown -R "$APP_USER:$APP_USER" "$BASE_DIR"
  fi
}

ensure_vcpkg() {
  if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    as_app git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
    as_app "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
  fi
}

ensure_repo() {
  if [[ ! -d "$REPO_DIR/.git" ]]; then
    as_app git clone --branch "$BRANCH" "$REPO_URL" "$REPO_DIR"
  fi
}

build_repo() {
  as_app env VCPKG_ROOT="$VCPKG_ROOT" cmake -S "$REPO_DIR" -B "$REPO_DIR/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPOINTERVERSE_BUILD_TESTS=OFF \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
  as_app cmake --build "$REPO_DIR/build" --target pointerverse
}

prebuild_pack() {
  local pack="$1"
  as_app env POINTERVERSE_PORT="$PORT" "$REPO_DIR/build/pointerverse" pack run "$pack" >/dev/null
}

ensure_user_and_dirs
ensure_vcpkg
ensure_repo

cd "$REPO_DIR"
old_head="$(as_app git rev-parse HEAD 2>/dev/null || true)"
as_app git fetch origin "$BRANCH"
as_app git checkout "$BRANCH"
as_app git pull --ff-only origin "$BRANCH"
new_head="$(as_app git rev-parse HEAD)"

if [[ "$old_head" != "$new_head" || ! -x "$REPO_DIR/build/pointerverse" ]]; then
  build_repo
  prebuild_pack mohacs || true
  prebuild_pack rust_wipe_blackbox || true
  if [[ "$(id -u)" == "0" ]]; then
    systemctl restart pointerverse.service 2>/dev/null || true
  fi
fi
