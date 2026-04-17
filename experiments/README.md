# Experiments Overview

This directory contains one folder per experiment stage and the shared execution entrypoints used across all stages. Runnable stages share the same simulator source trees and differ by `sb.cfg`.

## Experiment Folder Structure

Most runnable stages (`01` to `09`) follow this layout:

```text
experiments/<stage-id>/
├── sb.cfg
├── raw-manifest.csv
├── README.md
├── processed/
│   └── bandwidth_latency.csv
└── figures/
    ├── bandwidth_latency_ramulator.pdf
    ├── bandwidth_latency_zsim_mem.pdf
    ├── bandwidth_latency_zsim_core.pdf
    └── pngs/
```

`00-system-agnostic` is a structural seed/provenance stage and does not follow the full runnable layout (for example, it has no `sb.cfg` sweep config).

The committed outputs follow the three-view terminology used in the paper:

- `bandwidth_latency_ramulator.*`: memory simulator view
- `bandwidth_latency_zsim_mem.*`: memory interface view
- `bandwidth_latency_zsim_core.*`: application view

The `raw-manifest.csv` files list the externally hosted raw artifact for each stage.

## Shared Entrypoints

The following files live directly under `experiments/` and are shared across all stages:

| File | Purpose |
| :--- | :--- |
| `runner.sh` | Main local execution loop. Copies `sb.cfg` into each `measurment_*/` directory, adjusts shared-source paths, and runs the sweep sequentially. Invoke as `./experiments/runner.sh <stage-id>` from the repository root. |
| `run-one.sh` | Minimal per-run launcher copied into each generated `measurment_*/` directory by `runner.sh`. It is the single-run command body for local execution and can also be wrapped by a generic batch scheduler if desired. |
| `plot.py` | Post-processing script. Reads raw outputs, writes `processed/bandwidth_latency.csv`, generates `figures/`, and also writes PNG previews under `figures/pngs/`. If the memory simulator view is unavailable, it skips that plot instead of failing. By default it writes into `test-output/<stage-or-config-name>/`, not into the committed experiment folder. |

## Running the Experiments

### `runner.sh`

Runs the full bandwidth-latency sweep for one stage. Each measurement point is materialized as a `measurment_<rd>_<pause>/` directory inside `<stage>/test-raw/`.

```bash
# From the repository root (environment must be sourced first)
source .zsim-env
./experiments/runner.sh 01-baseline
```

After the run, the layout is:

```text
experiments/01-baseline/test-raw/
├── measurment_0_10000/
│   ├── sb.cfg
│   ├── zsim.out
│   └── ...
├── measurment_10_10000/
└── ...
```

`test-raw/` is gitignored; it never overwrites the committed paper results.

Before generating new run directories, `runner.sh` removes any existing `test-raw/measurment_*` directories for the selected stage.

`00-system-agnostic` is not intended for this shared runner flow. Use stages `01` to `09` with `runner.sh`.

`08-portability-ramulator2` has one extra requirement: ZSim must be rebuilt with Ramulator2-only linkage (unset `RAMULATORPATH` before rebuilding). `runner.sh` checks this and prompts before continuing in mixed environments.

### `run-one.sh`

Minimal single-point launcher. `runner.sh` invokes it automatically, but you can also call it manually for a prepared run directory:

```bash
cd ./experiments/01-baseline/test-raw/measurment_50_1000
bash run-one.sh
```

### `plot.py`

Post-processes a directory of `measurment_*` subdirs into a CSV and figures. Pass the `test-raw/` directory as the positional argument:

```bash
./experiments/plot.py experiments/01-baseline/test-raw \
  --config-dir experiments/01-baseline
```

Outputs by default to:

```text
test-output/01-baseline/
├── processed/bandwidth_latency.csv
└── figures/
    ├── bandwidth_latency_zsim_core.pdf
    ├── bandwidth_latency_zsim_mem.pdf
    ├── bandwidth_latency_ramulator.pdf   # only if memory-simulator stats present
    └── pngs/
```

The committed paper figures in `experiments/<stage>/figures/` are **not** touched. To regenerate them in-place:

```bash
./experiments/plot.py experiments/01-baseline/test-raw \
  --config-dir experiments/01-baseline \
  --output-dir experiments/01-baseline
```

### Comparing stages

Use `compare-results.sh` to quantify the delta between two stages from their committed (or freshly generated) `processed/bandwidth_latency.csv`:

```bash
./scripts/compare-results.sh 01-baseline 04-model-correct
```

You can also point it at a freshly generated CSV:

```bash
./scripts/compare-results.sh \
  test-output/01-baseline/processed/bandwidth_latency.csv \
  test-output/04-model-correct/processed/bandwidth_latency.csv
```

## Experiment Gallery

This section is stage-oriented, not paper-subfigure-oriented. Each populated stage shows every committed view that exists for that experiment. The paper may use only a subset of these views in a given figure.

### `01-baseline`

Paper figure: Figure 2  
Raw manifest: [`01-baseline/raw-manifest.csv`](01-baseline/raw-manifest.csv)

| Memory simulator view | Memory interface view | Application view |
|:---:|:---:|:---:|
| <img src="01-baseline/figures/pngs/bandwidth_latency_ramulator.png" height="220"> | <img src="01-baseline/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="01-baseline/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |

### `02-clock-uncommented`

Paper figure: Figure 3  
Raw manifest: [`02-clock-uncommented/raw-manifest.csv`](02-clock-uncommented/raw-manifest.csv)

| Memory simulator view | Memory interface view | Application view |
|:---:|:---:|:---:|
| <img src="02-clock-uncommented/figures/pngs/bandwidth_latency_ramulator.png" height="220"> | <img src="02-clock-uncommented/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="02-clock-uncommented/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |

### `03-clock-correct`

Paper figure: Figure 4  
Raw manifest: [`03-clock-correct/raw-manifest.csv`](03-clock-correct/raw-manifest.csv)

| Memory simulator view | Memory interface view | Application view |
|:---:|:---:|:---:|
| <img src="03-clock-correct/figures/pngs/bandwidth_latency_ramulator.png" height="220"> | <img src="03-clock-correct/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="03-clock-correct/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |

### `04-model-correct`

Paper figure: Figure 5  
Raw manifest: [`04-model-correct/raw-manifest.csv`](04-model-correct/raw-manifest.csv)

| Memory simulator view | Memory interface view | Application view |
|:---:|:---:|:---:|
| <img src="04-model-correct/figures/pngs/bandwidth_latency_ramulator.png" height="220"> | <img src="04-model-correct/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="04-model-correct/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |

### `05-address-mapping`

Paper figure: Figure 6a  
Raw manifest: [`05-address-mapping/raw-manifest.csv`](05-address-mapping/raw-manifest.csv)

| Memory simulator view | Memory interface view | Application view |
|:---:|:---:|:---:|
| <img src="05-address-mapping/figures/pngs/bandwidth_latency_ramulator.png" height="220"> | <img src="05-address-mapping/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="05-address-mapping/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |

### `06-noc`

Paper figure: Figure 6b  
Raw manifest: [`06-noc/raw-manifest.csv`](06-noc/raw-manifest.csv)

| Memory simulator view | Memory interface view | Application view |
|:---:|:---:|:---:|
| <img src="06-noc/figures/pngs/bandwidth_latency_ramulator.png" height="220"> | <img src="06-noc/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="06-noc/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |

### `07-prefetcher`

Paper figure: Figure 6c  
Raw manifest: [`07-prefetcher/raw-manifest.csv`](07-prefetcher/raw-manifest.csv)

| Memory simulator view | Memory interface view | Application view |
|:---:|:---:|:---:|
| <img src="07-prefetcher/figures/pngs/bandwidth_latency_ramulator.png" height="220"> | <img src="07-prefetcher/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="07-prefetcher/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |

### `08-portability-ramulator2`

Paper figure: Figure 7a  
Committed views: memory interface and application  
Raw manifest: [`08-portability-ramulator2/raw-manifest.csv`](08-portability-ramulator2/raw-manifest.csv)

| Memory interface view | Application view |
|:---:|:---:|
| <img src="08-portability-ramulator2/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="08-portability-ramulator2/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |

### `09-portability-dramsim3`

Paper figure: Figure 7b  
Committed views: memory interface and application  
Raw manifest: [`09-portability-dramsim3/raw-manifest.csv`](09-portability-dramsim3/raw-manifest.csv)

| Memory interface view | Application view |
|:---:|:---:|
| <img src="09-portability-dramsim3/figures/pngs/bandwidth_latency_zsim_mem.png" height="220"> | <img src="09-portability-dramsim3/figures/pngs/bandwidth_latency_zsim_core.png" height="220"> |
