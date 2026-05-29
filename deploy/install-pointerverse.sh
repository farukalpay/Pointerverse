#!/usr/bin/env bash
set -euo pipefail

if [[ "$(id -u)" != "0" ]]; then
  echo "install-pointerverse.sh must run as root" >&2
  exit 1
fi

APP_USER="${POINTERVERSE_USER:-pointerverse}"
BASE_DIR="${POINTERVERSE_BASE_DIR:-/opt/pointerverse}"
REPO_DIR="${POINTERVERSE_REPO_DIR:-$BASE_DIR/repo}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

if ! id "$APP_USER" >/dev/null 2>&1; then
  useradd --system --home "$BASE_DIR" --shell /usr/sbin/nologin "$APP_USER"
fi

mkdir -p "$BASE_DIR"
chown -R "$APP_USER:$APP_USER" "$BASE_DIR"

install -m 0755 "$SCRIPT_DIR/update-pointerverse.sh" /usr/local/bin/update-pointerverse
install -m 0644 "$SCRIPT_DIR/systemd/pointerverse.service" /etc/systemd/system/pointerverse.service
install -m 0644 "$SCRIPT_DIR/systemd/pointerverse-update.service" /etc/systemd/system/pointerverse-update.service
install -m 0644 "$SCRIPT_DIR/systemd/pointerverse-update.timer" /etc/systemd/system/pointerverse-update.timer

systemctl daemon-reload
/usr/local/bin/update-pointerverse
systemctl enable --now pointerverse.service
systemctl enable --now pointerverse-update.timer

echo "Pointerverse installed at $REPO_DIR"
