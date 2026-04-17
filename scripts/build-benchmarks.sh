#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_root="$(cd "$script_dir/.." && pwd -P)"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "build-benchmarks.sh requires Linux." >&2
  echo "The ptr_chase benchmark depends on linux/perf_event.h, and the full artifact flow is intended for the same Linux environment used for the paper runs." >&2
  exit 1
fi

(
  cd "$repo_root/benchmarks/ptr_chase"
  make
)

(
  cd "$repo_root/benchmarks/traffic_gen"
  make
)

echo "Benchmarks built under $repo_root/benchmarks"
