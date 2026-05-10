#!/usr/bin/env bash
set -euo pipefail

SCRIPT_REAL="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_REAL")" && pwd -P)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"
REPO_ROOT="$(cd "$EXPERIMENT_DIR/../.." && pwd -P)"
SIMULATOR_DIR="$EXPERIMENT_DIR/damov-src/simulator"
ENV_FILE="$REPO_ROOT/.zsim-env"
DAMOV_BENCH_DIR="$EXPERIMENT_DIR/benchmarks"
DAMOV_TRAFFIC_GEN_DIR="$DAMOV_BENCH_DIR/traffic_gen"
DAMOV_TRAFFIC_GEN="$DAMOV_TRAFFIC_GEN_DIR/traffic_gen.x"

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

echo "Building experiment 00 traffic_gen benchmark with CORE_NUMBER=31"
mkdir -p "$DAMOV_BENCH_DIR"
rm -rf "$DAMOV_TRAFFIC_GEN_DIR"
cp -a "$REPO_ROOT/benchmarks/traffic_gen" "$DAMOV_TRAFFIC_GEN_DIR"
python3 - <<PY
from pathlib import Path

path = Path("$DAMOV_TRAFFIC_GEN_DIR/src/traffic_gen.c")
text = path.read_text()
old = "#define CORE_NUMBER 23"
new = "#define CORE_NUMBER 31"
if old not in text:
    raise SystemExit(f"Could not find {old!r} in {path}")
path.write_text(text.replace(old, new, 1))
PY

make -C "$DAMOV_TRAFFIC_GEN_DIR" clean
make -C "$DAMOV_TRAFFIC_GEN_DIR"
echo "Experiment 00 traffic_gen benchmark built: $DAMOV_TRAFFIC_GEN"
