#!/usr/bin/env bash
set -euo pipefail

if (($# != 3)); then
    printf 'Usage: %s TOOLKIT_RUN OPS_310P_RUN NEW_OR_EMPTY_INSTALL_PREFIX\n' "$0" >&2
    exit 1
fi
TOOLKIT="$(realpath -e "$1")"
OPS="$(realpath -e "$2")"
PREFIX="$(realpath -m "$3")"

[[ "$(basename "${TOOLKIT}")" == 'Ascend-cann-toolkit_9.0.0_linux-x86_64.run' ]] || {
    printf 'Unexpected Toolkit package name: %s\n' "${TOOLKIT}" >&2; exit 1;
}
[[ "$(basename "${OPS}")" == 'Ascend-cann-310p-ops_9.0.0_linux-x86_64.run' ]] || {
    printf 'Unexpected ops package name: %s\n' "${OPS}" >&2; exit 1;
}

check_hash() {
    local path="$1"
    local expected="$2"
    local actual
    actual="$(sha256sum "${path}" | cut -d' ' -f1)"
    if [[ "${actual}" != "${expected}" ]]; then
        printf 'SHA-256 mismatch: %s\nexpected: %s\nactual:   %s\n' \
            "${path}" "${expected}" "${actual}" >&2
        exit 1
    fi
}
check_hash "${TOOLKIT}" '56f0ea2cc3c193921bcd35b814ef635b668d1bce349fdb6a04cb0ec4b6f1bc06'
check_hash "${OPS}" '30e90ef5edbb6ee887003e94b4f4030ea2baa0801ca4e73dc2af0829f278cd28'

if [[ -e "${PREFIX}" && ! -d "${PREFIX}" ]]; then
    printf 'Not a directory: %s\n' "${PREFIX}" >&2
    exit 1
fi
if [[ -d "${PREFIX}" && -n "$(find "${PREFIX}" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
    printf 'Refusing to overwrite a non-empty installation prefix: %s\n' "${PREFIX}" >&2
    exit 1
fi
mkdir -p "${PREFIX}/installer-home"

printf 'Installing CANN Toolkit as %s into %s\n' "$(id -un)" "${PREFIX}"
printf 'The quiet installer accepts the package EULA.\n'
HOME="${PREFIX}/installer-home" bash "${TOOLKIT}" \
    --full --quiet --nox11 --install-path="${PREFIX}"
HOME="${PREFIX}/installer-home" bash "${OPS}" \
    --full --quiet --nox11 --install-path="${PREFIX}"

SET_ENV="$(find "${PREFIX}" -maxdepth 4 -type f -name set_env.sh -print -quit)"
if [[ -z "${SET_ENV}" ]]; then
    printf 'Installers finished but set_env.sh was not found.\n' >&2
    exit 2
fi
CANN_HOME="$(dirname "${SET_ENV}")"
printf '\nInstallation complete. For the current shell, run:\n'
printf 'export CANN_HOME=%q\n' "${CANN_HOME}"
printf 'set +u; source "${CANN_HOME}/set_env.sh"; set -u\n'
