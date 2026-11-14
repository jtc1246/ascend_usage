#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
    printf 'Usage: %s NEW_OR_EMPTY_CMAKE_VENV_DIR\n' "$0" >&2
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

python3 -m venv "${DEST}"
"${DEST}/bin/python" -m pip install --disable-pip-version-check 'cmake==4.4.4'
"${DEST}/bin/cmake" --version
printf '\nUse this CMake with:\nexport CMAKE_BIN=%q\n' "${DEST}/bin/cmake"
