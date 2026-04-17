# Different Perspectives of Memory System Simulation

This repository is the public artifact repository for the paper on CPU-memory simulator interface correctness.

> Esmaili-Dokht, P., Yadegari, A., Xirau, V., Pavon, J., Cristal, A., Ayguadé, E., & Radojković, P. (2026). Different perspectives of memory system simulation.

This repository shares the final, corrected simulator source code, the benchmark source code, the committed processed results, and the scripts needed to rerun or compare those stages. It is designed to allow easy reproduction of the exact environment and results discussed in the paper.

## Table of Contents
- [1. Repository Architecture](#1-repository-architecture)
- [2. Environment Setup](#2-environment-setup)
- [3. Experiment Reproduction](#3-experiment-reproduction)
- [4. Result Comparison](#4-result-comparison)
- [5. Raw Data Policy](#5-raw-data-policy)
- [6. License](#6-license)

---

## 1. Repository Architecture

The repository is organized so that the source code is shared once, but is highly configurable via configuration files. This allows each experiment to independently activate or deactivate specific interface behaviors without duplicating the codebase. 

| Directory | Purpose & Documentation |
| :--- | :--- |
| `simulator-source/` | **The Simulators.** Contains ZSim, DRAMsim3, Ramulator, and Ramulator2. These are the final, corrected versions with all changes built-in. <br>-> *See [`simulator-source/README.md`](simulator-source/README.md) for build instructions and environment setup.* |
| `benchmarks/` | **The Workloads.** Contains the pointer-chasing and traffic-generation benchmarks used to generate Bandwidth-Latency curves based on the [Mess methodology](https://mess.bsc.es/). |
| `experiments/` | **The Configurations & Results.** One folder per paper stage. Runnable stages (`01` to `09`) include `sb.cfg`. Committed outputs live under `processed/` and `figures/`. <br>-> *See [`experiments/README.md`](experiments/README.md) for details on the execution flow and shared run entrypoints.* |
| `scripts/` | **The Automation.** Repository-level helpers for environment setup, benchmark builds, result processing, and comparison. <br>-> *See [`scripts/README.md`](scripts/README.md) for the script catalog.* |

Due to the heavy nature of raw simulator outputs, full raw traces are hosted externally. For details on what is committed versus what is kept external, please refer to the [Raw Data Policy](#5-raw-data-policy) section.

---

## 2. Environment Setup

Run the single entry-point script from the repository root on a **Linux** machine:

```bash
./setup.sh
```

This handles everything in sequence:
1. **Checks system dependencies** — GCC, cmake, scons, libconfig++, Python 3 with pandas/matplotlib
2. **Generates `.zsim-env`** — auto-resolves in-repo paths and tries to locate Pin/HDF5; if missing, `scripts/setup-env.sh` downloads the artifact's own Pin 2.14 and HDF5 1.8.16 bundles into `dependencies/`
3. **Builds memory simulators** — compiles `libramulator.so`, `libdramsim3.so`, `libramulator2.so`
4. **Builds ZSim** — release binary at `simulator-source/zsim-bsc/build/release/zsim`
5. **Builds benchmarks** — `ptr_chase` and `traffic_gen` under `benchmarks/`

To force a clean rebuild after pulling changes:

```bash
./setup.sh --rebuild
```

> **System requirements:** Linux, GCC, cmake, scons, libconfig++, Python 3 with pandas and matplotlib, plus network access if Pin/HDF5 must be auto-downloaded. `ptr_chase` requires `linux/perf_event.h`.
>
> **Pin on modern kernels:** Pin 2.14 may refuse to start on Linux 4.0+ kernels. Pass `-injection child` to work around the version check. See [`simulator-source/README.md`](simulator-source/README.md) for details.

-> *For manual dependency/build steps see [`simulator-source/README.md`](simulator-source/README.md). For script-by-script setup details see [`scripts/README.md`](scripts/README.md).*

---

## 3. Experiment Reproduction

The paper evaluates the impact of interface details through a sequence of cumulative refinements. Each stage represents a specific correction or enhancement to the simulator coupling:

### 3.1. Interface Refinement Stages
| Stage | Description / Focus | Figure |
| :--- | :--- | :--- |
| [`00-system-agnostic`](experiments/00-system-agnostic/) | Structural seed stage kept for provenance (not part of the runnable `runner.sh` pipeline) | N/A |
| [`01-baseline`](experiments/01-baseline/) | Base simulator coupling | Figure 2 |
| [`02-clock-uncommented`](experiments/02-clock-uncommented/) | Unsynchronized clock domains | Figure 3 |
| [`03-clock-correct`](experiments/03-clock-correct/) | Synchronized clock domains | Figure 4 |
| [`04-model-correct`](experiments/04-model-correct/) | Correct memory model timing | Figure 5 |
| [`05-address-mapping`](experiments/05-address-mapping/) | Physical address mapping accuracy | Figure 6a |
| [`06-noc`](experiments/06-noc/) | Realistic Network-on-Chip refinement | Figure 6b |
| [`07-prefetcher`](experiments/07-prefetcher/) | Final Ramulator stage with prefetcher | Figure 6c |

### 3.2. Portability Evaluation
| Stage | Description / Focus | Figure |
| :--- | :--- | :--- |
| [`08-portability-ramulator2`](experiments/08-portability-ramulator2/) | Evaluation using Ramulator2 | Figure 7a |
| [`09-portability-dramsim3`](experiments/09-portability-dramsim3/) | Evaluation using DRAMsim3 | Figure 7b |

### 3.3. Running and Plotting

After `./setup.sh` completes, the full cycle for one stage is:

```bash
# Source the environment (once per shell session)
source .zsim-env

# Run the full sweep — results land in experiments/01-baseline/test-raw/
./experiments/runner.sh 01-baseline

# Generate figures and a processed CSV from your run
./experiments/plot.py experiments/01-baseline/test-raw \
  --config-dir experiments/01-baseline
# → writes to test-output/01-baseline/processed/ and test-output/01-baseline/figures/
```

`runner.sh` clears prior `test-raw/measurment_*` directories for the selected stage before creating a fresh run.

For `08-portability-ramulator2`, use a ZSim build configured for Ramulator2-only linkage (unset `RAMULATORPATH` and rebuild ZSim). `runner.sh` checks this and warns interactively if the environment is mixed.

The committed paper figures are under `experiments/<stage>/figures/` and are not touched by the commands above. To overwrite them intentionally:

```bash
./experiments/plot.py experiments/01-baseline/test-raw \
  --config-dir experiments/01-baseline \
  --output-dir experiments/01-baseline
```

-> *For the full execution model see [`experiments/README.md`](experiments/README.md).*

---

## 4. Result Comparison

A key contribution of the paper is analyzing the delta between interface correctness stages. 

To compare the output of two different stages (e.g., comparing the baseline against the corrected model), use the `compare-results.sh` script:

```bash
./scripts/compare-results.sh 01-baseline 04-model-correct
```
It can also compare two explicit CSV files (for example from `test-output/.../processed/bandwidth_latency.csv`).

---

## 5. Raw Data Policy

To balance reproducibility with repository size, this artifact distinguishes between data that is committed to version control and data that is hosted externally. The committed public result of each experiment is its processed CSV under `processed/` plus the PDF figure set under `figures/`, while the raw simulation traces are available for download on demand.

### 5.1. Data Committed to Version Control
- **Experiment configurations:** `sb.cfg` files for runnable stages (`01` to `09`), with per-stage notes in each experiment README
- **Shared automation:** Repository-level helper scripts and entrypoints
- **Processed outputs:** CSV tables in each experiment's `processed/` folder and PDF figures in `figures/`
- **Provenance metadata:** Per-experiment `raw-manifest.csv` files tracking external data sources

### 5.2. Data Hosted Externally
To keep the repository lightweight and avoid Git's storage limitations, the following are omitted from version control:
- Full `measuring/bw-lat/` directories containing raw simulation traces
- Complete bulk simulator outputs and HDF5 result sets
- Large repeated log files and intermediate artifacts
- Legacy `output/` PDFs and `processing/.../plots/` directories that duplicate figures already committed in the artifact

### 5.3. Accessing Raw Data
Raw-data provenance is tracked per experiment in `experiments/<id>/raw-manifest.csv`. Each manifest records the raw artifact role and publication status.

The committed manifests point at the Zenodo record that hosts the raw traces for this artifact release.

The current externally hosted raw artifact model is:
- one `tar.gz` archive per experiment stage in the Zenodo record `10.5281/zenodo.19629352`

The original `config.sh`, `output/`, and `processing/` trees are intentionally left out of the published raw-data manifests because they are either already represented elsewhere in the artifact or are legacy intermediate artifacts not needed for public reproduction.

Figure 6a is one special case worth calling out. The original experiment drop implemented the address-mapping change through a source-only Ramulator toggle. In this artifact, that behavior is exposed through `simulator-source/ramulator/ramulator-configs/DDR4-config-MN4-skylake.cfg`, so the address-mapping stage can be reproduced through configuration rather than by editing source comments by hand.

If you prefer to download pre-computed raw data instead of running the simulations yourself, you can use the provided script:
```bash
./scripts/download-raw.sh 01-baseline
```

---

## 6. License

This artifact is distributed under the BSD 3-Clause License.