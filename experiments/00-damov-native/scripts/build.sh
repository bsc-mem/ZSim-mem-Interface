#!/usr/bin/env bash
set -euo pipefail

SCRIPT_REAL="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_REAL")" && pwd -P)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"
REPO_ROOT="$(cd "$EXPERIMENT_DIR/../.." && pwd -P)"
SIMULATOR_DIR="$EXPERIMENT_DIR/damov-src/simulator"
ENV_FILE="$REPO_ROOT/.zsim-env"

if [[ ! -d "$SIMULATOR_DIR" ]]; then
  echo "Missing DAMOV simulator directory: $SIMULATOR_DIR" >&2
  exit 1
fi

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing $ENV_FILE; generating it with scripts/setup-env.sh..."
  "$REPO_ROOT/scripts/setup-env.sh"
fi

source "$ENV_FILE"

echo "Building DAMOV native simulator from: $SIMULATOR_DIR"
(
  cd "$SIMULATOR_DIR"
  bash ./scripts/compile.sh
)
echo "DAMOV native simulator build completed."
