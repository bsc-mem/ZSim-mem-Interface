#!/usr/bin/env bash
set -euo pipefail

SCRIPT_REAL="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_REAL")" && pwd -P)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"
SIMULATOR_DIR="$EXPERIMENT_DIR/damov-src/simulator"

if [[ ! -d "$SIMULATOR_DIR" ]]; then
  echo "Missing DAMOV simulator directory: $SIMULATOR_DIR" >&2
  exit 1
fi

if [[ "$EUID" -ne 0 ]]; then
  echo "DAMOV build requires root permission (simulator/scripts/setup.sh installs packages)." >&2
  echo "Run from repository root: sudo ./setup.sh --build-damov" >&2
  exit 1
fi

echo "Building DAMOV native simulator from: $SIMULATOR_DIR"
(
  cd "$SIMULATOR_DIR"
  bash ./scripts/setup.sh || true
  bash ./scripts/compile.sh
)
echo "DAMOV native simulator build completed."
