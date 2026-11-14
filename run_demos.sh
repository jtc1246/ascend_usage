#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CANN_HOME="${CANN_HOME:-/usr/local/Ascend/ascend-toolkit/latest}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/demos/build}"
set +u
source "${CANN_HOME}/set_env.sh"
set -u

devices=("$@")
if ((${#devices[@]} == 0)); then
    devices=(0 1)
fi
for device in "${devices[@]}"; do
    "${BUILD_DIR}/acl_memory_demo" "${device}"
    "${BUILD_DIR}/acl_gemm_demo" "${device}"
    "${BUILD_DIR}/ascendc_add_demo" "${device}"
done
