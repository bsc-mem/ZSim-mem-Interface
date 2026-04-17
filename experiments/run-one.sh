#!/usr/bin/env bash
set -euo pipefail

# Per-run launcher used by runner.sh after a measurement directory has been
# created. The same file can also be wrapped by a generic batch scheduler once
# the run directory is prepared, but the public artifact uses it primarily as
# the local single-run entrypoint.

export OMP_NUM_THREADS=23

find_zsim_bin() {
  for prefix in . .. ../.. ../../.. ../../../..; do
    candidate="$prefix/simulator-source/zsim-bsc/build/release/zsim"
    if [[ -x "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

zsim_bin="$(find_zsim_bin || true)"
if [[ -z "$zsim_bin" ]]; then
  echo "Unable to locate simulator-source/zsim-bsc/build/release/zsim from $(pwd)." >&2
  exit 1
fi

config_file="sb.cfg"
if [[ ! -f "$config_file" && -f "system.cfg" ]]; then
  config_file="system.cfg"
fi

"$zsim_bin" "$config_file"
