# Scripts Overview

This directory contains repository-level automation helpers. Experiment execution entrypoints (`runner.sh`, `run-one.sh`, `plot.py`) are kept under `experiments/` by design.

## Available Scripts

### `../setup.sh` (repo root)
Primary one-shot entrypoint for first-time setup and rebuilds. It wraps dependency checks, environment generation, simulator builds, and benchmark builds.

```bash
./setup.sh
# or
./setup.sh --rebuild
```

### `setup-env.sh`
Generates the `.zsim-env` file at the repository root. The three memory-simulator paths (`DRAMSIM3PATH`, `RAMULATORPATH`, `RAMULATOR2PATH`) are resolved automatically from the repo. `PINPATH` and `HDF5_HOME` are discovered from common system prefixes; if missing, the script attempts to download known-good bundles into `dependencies/`.

```bash
./scripts/setup-env.sh
source .zsim-env
```

This must be run once before building ZSim or running any experiment. See [`simulator-source/README.md`](../simulator-source/README.md) for dependency details.

### `build-benchmarks.sh`
Compiles the shared `ptr_chase` and `traffic_gen` benchmarks under `benchmarks/`. This must be run before any experiment can execute.

This script is intended for the Linux environment used in the paper artifact. In particular, `ptr_chase` depends on `linux/perf_event.h`.

```bash
./scripts/build-benchmarks.sh
```

### `compare-results.sh`
Compares two experiment stages by analyzing their `processed/bandwidth_latency.csv` outputs side by side. Useful for quantifying the performance delta introduced by each interface correction.

```bash
./scripts/compare-results.sh <stage-a> <stage-b>
# Example:
./scripts/compare-results.sh 01-baseline 04-model-correct
```

### `download-raw.sh`
Downloads the externally hosted raw artifacts for a given stage when URLs are published in the stage's `raw-manifest.csv`. See the [Raw Data Policy](../README.md#5-raw-data-policy) for details on what is hosted externally.

```bash
./scripts/download-raw.sh <stage>
# Example:
./scripts/download-raw.sh 01-baseline
```

Internal helper:
- `lib/compare_results.py` — implementation used by `compare-results.sh`

## Typical Workflow

**First time — build everything:**
```bash
./setup.sh
```

**Run a single stage and plot it:**
```bash
source .zsim-env
./experiments/runner.sh 01-baseline
./experiments/plot.py experiments/01-baseline/test-raw \
  --config-dir experiments/01-baseline
# → figures land in test-output/01-baseline/figures/
```

**Compare two stages** (works against committed CSVs or freshly generated ones):
```bash
./scripts/compare-results.sh 01-baseline 04-model-correct
```

**Download published raw artifacts for one stage** (if manifest URLs are final):
```bash
./scripts/download-raw.sh 01-baseline
```

-> *For details on `runner.sh`, `run-one.sh`, and the plotting pipeline see [`experiments/README.md`](../experiments/README.md).*
