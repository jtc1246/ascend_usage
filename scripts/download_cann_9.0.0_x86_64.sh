#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if (($# != 1)); then
    printf 'Usage: %s NEW_OR_EMPTY_DOWNLOAD_DIR\n' "$0" >&2
    exit 1
fi
DEST="$(realpath -m "$1")"
if [[ -e "${DEST}" && ! -d "${DEST}" ]]; then
    printf 'Not a directory: %s\n' "${DEST}" >&2
    exit 1
fi
if [[ -d "${DEST}" && -n "$(find "${DEST}" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
    printf 'Refusing to use a non-empty directory: %s\n' "${DEST}" >&2
    exit 1
fi
mkdir -p "${DEST}"

BASE_URL='https://ascend-repo.obs.cn-east-2.myhuaweicloud.com/CANN/CANN%209.0.0'
packages=(
    Ascend-cann-toolkit_9.0.0_linux-x86_64.run
    Ascend-cann-310p-ops_9.0.0_linux-x86_64.run
)
for package in "${packages[@]}"; do
    curl --fail --location --proto '=https' --tlsv1.2 \
        --output "${DEST}/${package}" "${BASE_URL}/${package}"
done

cd "${DEST}"
sha256sum --check "${ROOT}/scripts/cann-9.0.0-x86_64.sha256"
printf '\nVerified packages are in %s\n' "${DEST}"
