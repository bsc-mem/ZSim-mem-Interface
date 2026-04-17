#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
if [[ -f "${SCRIPT_DIR}/.zsim-env" ]]; then
  # shellcheck disable=SC1091
  source "${SCRIPT_DIR}/.zsim-env"
fi
exec scons "$@"
