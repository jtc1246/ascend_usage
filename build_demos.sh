#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CANN_HOME="${CANN_HOME:-/usr/local/Ascend/ascend-toolkit/latest}"
CMAKE_BIN="${CMAKE_BIN:-cmake}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/demos/build}"

set +u
source "${CANN_HOME}/set_env.sh"
set -u
"${CMAKE_BIN}" -S "${ROOT}/demos" -B "${BUILD_DIR}" \
    -DASCEND_CANN_PACKAGE_PATH="${CANN_HOME}" \
    -DCMAKE_BUILD_TYPE=Release
"${CMAKE_BIN}" --build "${BUILD_DIR}" --parallel 4
