#!/usr/bin/env bash
set -euo pipefail

SCRIPT_REAL="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_REAL")" && pwd -P)"
SIMULATOR_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"
EXPERIMENT_DIR="$(cd "$SIMULATOR_DIR/../.." && pwd -P)"
REPO_ROOT="$(cd "$EXPERIMENT_DIR/../.." && pwd -P)"
ENV_FILE="$REPO_ROOT/.zsim-env"

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing $ENV_FILE; run scripts/setup-env.sh from the repository root first." >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$ENV_FILE"

export PINPATH RAMULATORPATH LIBCONFIGPATH="$SIMULATOR_DIR/libconfig"
(
  cd "$SIMULATOR_DIR"
  scons -c
)
