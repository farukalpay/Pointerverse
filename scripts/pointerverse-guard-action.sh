#!/usr/bin/env bash
set -euo pipefail

ACTION_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="${GITHUB_WORKSPACE:-$PWD}"
REPO_INPUT="${POINTERVERSE_GUARD_REPO:-.}"
BASE="${POINTERVERSE_GUARD_BASE:-origin/main}"
MODE="${POINTERVERSE_GUARD_MODE:-observe}"
REPORT="${POINTERVERSE_GUARD_REPORT:-audit-report.md}"
JSON_REPORT="${POINTERVERSE_GUARD_JSON:-audit-report.json}"
FORMAT="${POINTERVERSE_GUARD_FORMAT:-markdown}"
SARIF="${POINTERVERSE_GUARD_SARIF:-audit.sarif}"
COMMENT="${POINTERVERSE_GUARD_COMMENT:-true}"
ANNOTATIONS="${POINTERVERSE_GUARD_ANNOTATIONS:-true}"
VERSION="${POINTERVERSE_GUARD_VERSION:-latest}"
STORE="${POINTERVERSE_GUARD_STORE:-$WORKSPACE/.pvstore}"

if [[ -z "${GITHUB_TOKEN:-}" && -n "${POINTERVERSE_DEFAULT_GITHUB_TOKEN:-}" ]]; then
  GITHUB_TOKEN="$POINTERVERSE_DEFAULT_GITHUB_TOKEN"
fi

if [[ "$REPO_INPUT" = /* ]]; then
  TARGET_REPO="$REPO_INPUT"
else
  TARGET_REPO="$WORKSPACE/$REPO_INPUT"
fi

if [[ "$FORMAT" == "markdown" ]]; then
  MARKDOWN_REPORT="$REPORT"
else
  MARKDOWN_REPORT="${POINTERVERSE_GUARD_MARKDOWN_REPORT:-audit-report.md}"
  if [[ "$MARKDOWN_REPORT" == "$REPORT" ]]; then
    MARKDOWN_REPORT="pointerverse-guard.md"
  fi
fi

resolve_workspace_path() {
  local path="$1"
  if [[ -z "$path" ]]; then
    return 0
  fi
  if [[ "$path" = /* ]]; then
    printf '%s\n' "$path"
  else
    printf '%s/%s\n' "$WORKSPACE" "$path"
  fi
}

enabled() {
  case "$1" in
    true|True|TRUE|1|yes|Yes|YES|on|On|ON) return 0 ;;
    *) return 1 ;;
  esac
}

notice() {
  echo "::notice title=Pointerverse Guard::$1"
}

MARKDOWN_PATH="$(resolve_workspace_path "$MARKDOWN_REPORT")"
JSON_PATH="$(resolve_workspace_path "$JSON_REPORT")"
SARIF_PATH="$(resolve_workspace_path "$SARIF")"

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  {
    echo "report=$MARKDOWN_PATH"
    echo "json=$JSON_PATH"
    echo "sarif=$SARIF_PATH"
  } >> "$GITHUB_OUTPUT"
fi

download_release_binary() {
  if [[ "${RUNNER_OS:-$(uname -s)}" != "Linux" ]]; then
    return 1
  fi
  case "$(uname -m)" in
    x86_64|amd64) ;;
    *) return 1 ;;
  esac
  if [[ "$VERSION" == "source" ]]; then
    return 1
  fi

  local asset="pointerverse-linux-x86_64.tar.gz"
  local url
  if [[ "$VERSION" == "latest" ]]; then
    url="https://github.com/farukalpay/Pointerverse/releases/latest/download/$asset"
  else
    url="https://github.com/farukalpay/Pointerverse/releases/download/$VERSION/$asset"
  fi

  local download_dir="${RUNNER_TEMP:-/tmp}/pointerverse-guard-release"
  local archive="$download_dir/$asset"
  rm -rf "$download_dir"
  mkdir -p "$download_dir"

  echo "Pointerverse Guard: downloading $url" >&2
  if ! curl -fsSL --retry 3 "$url" -o "$archive"; then
    echo "Pointerverse Guard: release binary unavailable, falling back to source build" >&2
    return 1
  fi

  tar -xzf "$archive" -C "$download_dir"
  local binary
  binary="$(find "$download_dir" -type f -name pointerverse -perm -111 | head -n 1 || true)"
  if [[ -z "$binary" ]]; then
    return 1
  fi
  printf '%s\n' "$binary"
}

build_from_source() {
  local build_dir="${RUNNER_TEMP:-/tmp}/pointerverse-guard-build"
  if [[ -z "${VCPKG_ROOT:-}" ]] && command -v apt-get >/dev/null 2>&1 && command -v sudo >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y cmake g++ ninja-build libfmt-dev nlohmann-json3-dev libcli11-dev libssl-dev
  fi
  if [[ -n "${VCPKG_ROOT:-}" ]]; then
    cmake -S "$ACTION_ROOT" -B "$build_dir" -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      -DPOINTERVERSE_BUILD_TESTS=OFF
  else
    cmake -S "$ACTION_ROOT" -B "$build_dir" -G Ninja -DPOINTERVERSE_BUILD_TESTS=OFF
  fi
  cmake --build "$build_dir" --target pointerverse
  if [[ -x "$build_dir/pointerverse" ]]; then
    printf '%s\n' "$build_dir/pointerverse"
  else
    printf '%s\n' "$build_dir/apps/pointerverse/pointerverse"
  fi
}

resolve_binary() {
  if [[ -n "${POINTERVERSE_BIN:-}" ]]; then
    printf '%s\n' "$POINTERVERSE_BIN"
    return 0
  fi
  if [[ -x "$ACTION_ROOT/build/pointerverse" ]]; then
    printf '%s\n' "$ACTION_ROOT/build/pointerverse"
    return 0
  fi
  if [[ -x "$ACTION_ROOT/build/apps/pointerverse/pointerverse" ]]; then
    printf '%s\n' "$ACTION_ROOT/build/apps/pointerverse/pointerverse"
    return 0
  fi
  if binary="$(download_release_binary)"; then
    printf '%s\n' "$binary"
    return 0
  fi
  build_from_source
}

supports_multi_output() {
  "$1" guard run --help 2>&1 | grep -q -- "--json-out"
}

write_step_summary() {
  if [[ -n "${GITHUB_STEP_SUMMARY:-}" && -f "$MARKDOWN_PATH" ]]; then
    cat "$MARKDOWN_PATH" >> "$GITHUB_STEP_SUMMARY"
    printf '\n' >> "$GITHUB_STEP_SUMMARY"
  fi
}

emit_annotations() {
  if ! enabled "$ANNOTATIONS" || [[ ! -f "$JSON_PATH" ]]; then
    return 0
  fi
  if ! command -v node >/dev/null 2>&1; then
    notice "node is unavailable; skipping annotations"
    return 0
  fi
  node - "$JSON_PATH" <<'NODE'
const fs = require("fs");
const path = process.argv[2];

function escapeData(value) {
  return String(value ?? "")
    .replace(/%/g, "%25")
    .replace(/\r/g, "%0D")
    .replace(/\n/g, "%0A");
}

function escapeProperty(value) {
  return escapeData(value)
    .replace(/:/g, "%3A")
    .replace(/,/g, "%2C");
}

const report = JSON.parse(fs.readFileSync(path, "utf8"));
for (const finding of report.findings || []) {
  const severity = String(finding.severity || "info").toLowerCase();
  if (severity === "info") {
    continue;
  }
  const command = severity === "critical" || severity === "high" ? "error" : "warning";
  const props = ["title=Pointerverse Guard"];
  if (finding.file) {
    props.unshift(`file=${escapeProperty(finding.file)}`);
  }
  if (Number.isInteger(finding.line) && finding.line > 0) {
    props.push(`line=${finding.line}`);
  }
  const rule = finding.rule ? ` ${finding.rule}` : "";
  const message = `${severity.toUpperCase()}${rule}: ${finding.message || "Pointerverse Guard finding"}`;
  console.log(`::${command} ${props.join(",")}::${escapeData(message)}`);
}
NODE
}

post_pr_comment() {
  if ! enabled "$COMMENT" || [[ ! -f "$MARKDOWN_PATH" ]]; then
    return 0
  fi
  if [[ -z "${GITHUB_TOKEN:-}" || -z "${GITHUB_REPOSITORY:-}" || -z "${GITHUB_EVENT_PATH:-}" || ! -f "$GITHUB_EVENT_PATH" ]]; then
    notice "GitHub PR context is unavailable; skipping PR comment"
    return 0
  fi
  if ! command -v node >/dev/null 2>&1 || ! command -v curl >/dev/null 2>&1; then
    notice "node or curl is unavailable; skipping PR comment"
    return 0
  fi

  local pr_number
  pr_number="$(node - "$GITHUB_EVENT_PATH" <<'NODE'
const fs = require("fs");
const event = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const number = event.pull_request?.number || event.issue?.number || event.number || "";
if (number) {
  console.log(number);
}
NODE
)"
  if [[ -z "$pr_number" ]]; then
    notice "workflow event is not a pull request; skipping PR comment"
    return 0
  fi

  local api="${GITHUB_API_URL:-https://api.github.com}"
  local marker="<!-- pointerverse-guard-comment -->"
  local body_file="${RUNNER_TEMP:-/tmp}/pointerverse-guard-comment.md"
  local comments_file="${RUNNER_TEMP:-/tmp}/pointerverse-guard-comments.json"
  local payload_file="${RUNNER_TEMP:-/tmp}/pointerverse-guard-comment-payload.json"

  {
    echo "$marker"
    echo
    cat "$MARKDOWN_PATH"
  } > "$body_file"

  local comments_url="$api/repos/$GITHUB_REPOSITORY/issues/$pr_number/comments?per_page=100"
  if ! curl -fsS \
    -H "Authorization: Bearer $GITHUB_TOKEN" \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "$comments_url" > "$comments_file"; then
    notice "could not read PR comments; skipping PR comment update"
    return 0
  fi

  local comment_id
  comment_id="$(node - "$comments_file" "$marker" <<'NODE'
const fs = require("fs");
const comments = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const marker = process.argv[3];
const existing = Array.isArray(comments)
  ? comments.find((comment) => String(comment.body || "").includes(marker))
  : undefined;
if (existing?.id) {
  console.log(existing.id);
}
NODE
)"

  BODY_FILE="$body_file" node -e 'const fs = require("fs"); process.stdout.write(JSON.stringify({body: fs.readFileSync(process.env.BODY_FILE, "utf8")}));' > "$payload_file"

  if [[ -n "$comment_id" ]]; then
    if ! curl -fsS -X PATCH \
      -H "Authorization: Bearer $GITHUB_TOKEN" \
      -H "Accept: application/vnd.github+json" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      "$api/repos/$GITHUB_REPOSITORY/issues/comments/$comment_id" \
      -d @"$payload_file" >/dev/null; then
      notice "could not update Pointerverse PR comment"
    fi
  else
    if ! curl -fsS -X POST \
      -H "Authorization: Bearer $GITHUB_TOKEN" \
      -H "Accept: application/vnd.github+json" \
      -H "X-GitHub-Api-Version: 2022-11-28" \
      "$api/repos/$GITHUB_REPOSITORY/issues/$pr_number/comments" \
      -d @"$payload_file" >/dev/null; then
      notice "could not create Pointerverse PR comment"
    fi
  fi
}

BINARY="$(resolve_binary)"
if ! supports_multi_output "$BINARY"; then
  echo "Pointerverse Guard: selected binary does not support multi-output reports, falling back to source build" >&2
  BINARY="$(build_from_source)"
fi

cd "$WORKSPACE"
guard_args=(
  guard run
  --repo "$TARGET_REPO"
  --base "$BASE"
  --mode "$MODE"
  --format "$FORMAT"
  --store "$STORE"
  --json-out "$JSON_REPORT"
)

if [[ -n "$SARIF" ]]; then
  guard_args+=(--sarif-out "$SARIF")
fi

if [[ "$FORMAT" == "markdown" ]]; then
  guard_args+=(--markdown-out "$MARKDOWN_REPORT")
else
  guard_args+=(--out "$REPORT" --markdown-out "$MARKDOWN_REPORT")
fi

set +e
"$BINARY" "${guard_args[@]}"
guard_status=$?
set -e

write_step_summary
emit_annotations
post_pr_comment

exit "$guard_status"
