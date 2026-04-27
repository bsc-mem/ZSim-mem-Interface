#!/usr/bin/env bash
set -euo pipefail

SCRIPT_REAL="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_REAL")" && pwd -P)"
EXPERIMENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"
REPO_ROOT="$(cd "$EXPERIMENT_DIR/../.." && pwd -P)"
BENCH_ROOT="$REPO_ROOT/benchmarks"

export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8

find_damov_zsim_bin() {
  local candidate
  for candidate in \
    "$EXPERIMENT_DIR/damov-src/simulator/build/release/zsim" \
    "$EXPERIMENT_DIR/damov-src/simulator/build/opt/zsim"; do
    if [[ -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done

  return 1
}

ZSIM_BIN="$(find_damov_zsim_bin || true)"
if [[ -z "$ZSIM_BIN" ]]; then
  echo "Unable to locate a built damov zsim binary under:" >&2
  echo "  $EXPERIMENT_DIR/damov-src/simulator/build/{release,opt}/zsim" >&2
  echo "User '$(id -un)' cannot run 00-damov-native because DAMOV native is not built yet." >&2
  echo "Run from the repository root with root permission:" >&2
  echo "  sudo ./setup.sh --build-damov" >&2
  exit 1
fi

CONFIG_TEMPLATE="$EXPERIMENT_DIR/configs/system.cfg"
RAMULATOR_TEMPLATE="$EXPERIMENT_DIR/configs/DDR4-config.cfg"

RWRATIO_MIN=0
RWRATIO_MAX=100
RWRATIO_STEP=10
PAUSES="10000 2000 1000 0 5 10 15 20 25 30 35 40 45 50 55 60 65 70 80 90 100 120 140 160 180 200 220 260 300 340 380 450 550 600 700 800 900"

for required in "$CONFIG_TEMPLATE" "$RAMULATOR_TEMPLATE" "$BENCH_ROOT/ptr_chase/ptr_chase" "$BENCH_ROOT/ptr_chase/array.dat" "$BENCH_ROOT/traffic_gen/traffic_gen.x"; do
  if [[ ! -e "$required" ]]; then
    echo "Missing required file: $required" >&2
    echo "Run ./scripts/build-benchmarks.sh from the repository root before using runner.sh." >&2
    exit 1
  fi
done

RAW_DIR="$EXPERIMENT_DIR/test-raw"
mkdir -p "$RAW_DIR"
# rm -rf "$RAW_DIR"/measurment_*

for ((rd_percentage=RWRATIO_MIN; rd_percentage<=RWRATIO_MAX; rd_percentage+=RWRATIO_STEP)); do
  for pause in ${PAUSES}; do
    export rd_percentage
    export pause

    echo ""
    echo "*********** Iteration: rd_percentage=${rd_percentage} pause=${pause}"

    run_dir="$RAW_DIR/measurment_${rd_percentage}_${pause}"
    if [[ -d "$run_dir" ]]; then
      echo "Directory $(basename "$run_dir") exists, skipping."
      continue
    fi

    mkdir -p "$run_dir/output"
    cp "$CONFIG_TEMPLATE" "$run_dir/system.cfg"
    cp "$RAMULATOR_TEMPLATE" "$run_dir/DDR4-config.cfg"
    python3 - <<PY
from pathlib import Path
path = Path("$run_dir/system.cfg")
text = path.read_text()
text = text.replace("rd_percentage", "$rd_percentage")
text = text.replace("pause", "$pause")
text = text.replace("../configs/DDR4-config.cfg", "./DDR4-config.cfg")
text = text.replace("../../configs/DDR4-config.cfg", "./DDR4-config.cfg")
path.write_text(text)
PY

    cp "$BENCH_ROOT/ptr_chase/ptr_chase" "$run_dir/"
    cp "$BENCH_ROOT/ptr_chase/array.dat" "$run_dir/"
    cp "$BENCH_ROOT/traffic_gen/traffic_gen.x" "$run_dir/"

    (
      cd "$run_dir"
      time "$ZSIM_BIN" system.cfg > trace.txt
      rm -f array.dat traffic_gen.x
    )
  done
done
