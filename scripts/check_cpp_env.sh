#!/usr/bin/env bash
set -euo pipefail

CANN_HOME="${CANN_HOME:-/usr/local/Ascend/ascend-toolkit/latest}"
CMAKE_BIN="${CMAKE_BIN:-cmake}"
ARCH="$(uname -m)"
ARCH_DIR="${CANN_HOME}/${ARCH}-linux"

for command_name in bash g++ make python3 npu-smi; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        printf 'Missing command: %s\n' "${command_name}" >&2
        exit 1
    fi
done
if [[ ! -f "${CANN_HOME}/set_env.sh" ]]; then
    printf 'Missing CANN environment script: %s/set_env.sh\n' "${CANN_HOME}" >&2
    exit 1
fi
for path in \
    "${ARCH_DIR}/include/acl/acl.h" \
    "${ARCH_DIR}/include/aclnnop/aclnn_matmul.h" \
    "${ARCH_DIR}/lib64/libascendcl.so" \
    "${ARCH_DIR}/lib64/libopapi.so" \
    "${ARCH_DIR}/lib64/libnnopbase.so"; do
    if [[ ! -e "${path}" ]]; then
        printf 'Missing CANN file: %s\n' "${path}" >&2
        exit 1
    fi
done

set +u
source "${CANN_HOME}/set_env.sh"
set -u
if ! command -v ccec >/dev/null 2>&1; then
    printf 'Ascend C compiler ccec is not in PATH after sourcing set_env.sh\n' >&2
    exit 1
fi
if ! command -v "${CMAKE_BIN}" >/dev/null 2>&1; then
    printf 'CMake not found: %s\n' "${CMAKE_BIN}" >&2
    exit 1
fi

printf 'Architecture: %s\n' "${ARCH}"
printf 'CANN_HOME: %s\n' "${CANN_HOME}"
printf 'ASCEND_OPP_PATH: %s\n' "${ASCEND_OPP_PATH}"
printf 'CCEC: %s\n' "$(command -v ccec)"
printf 'CMake: %s\n' "$(command -v "${CMAKE_BIN}")"
g++ --version
npu-smi info
