#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${ROOT}/build/pointerverse_kernel_stress"
STORE="${ROOT}/examples/kernel_stress/.kernel_stress"

cmake --build "${ROOT}/build" --target pointerverse_kernel_stress
"${BIN}" "${STORE}"
"${ROOT}/build/pointerverse" repo --store "${STORE}" fsck
