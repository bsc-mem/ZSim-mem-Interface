# Shared Simulator Source

This directory contains the shared simulator source trees used across all artifact experiments. **Important:** the experiments are configuration-driven — all runnable stages share a single set of source trees and vary through `sb.cfg`. No source code is duplicated across experiments.

| Directory | Role |
| :--- | :--- |
| `zsim-bsc/` | Main ZSim simulator — the primary contribution of this artifact |
| `dramsim3/DRAMsim3/` | DRAMsim3 memory simulator, used by the DRAMsim3 portability stage |
| `ramulator/` | Ramulator memory simulator, used by the ZSim interface experiments |
| `ramulator2/` | Ramulator2 memory simulator, used by the Ramulator2 portability stage |

---

## 1. The ZSim-BSC Simulator

### 1.1. Lineage

The `zsim-bsc/` tree is a heavily extended fork of the [BSC ZSim fork](https://github.com/bsc-mem/zsim), and it is the only version that can correctly reproduce the results in this paper. Not just any ZSim version will work.

The development lineage is:

1. **Original ZSim** (Stanford/MIT): Fast x86-64 simulator introduced in the ISCA 2013 paper by Sanchez and Kozyrakis. Targeted very old microarchitectures.
2. **BSC fork** ([bsc-mem/zsim](https://github.com/bsc-mem/zsim)): Extended to Sandy Bridge, with updated instruction decoder, port latencies, and bandwidth parameters.
3. **Skylake extensions**: Further extended to support Intel Skylake microarchitecture — updated cache sizes and replacement policies. The methodology is documented in the appendix of the [Mess MICRO 2024 paper](https://mess.bsc.es/).
4. **This artifact's additions** (on top of `zsim-bsc`):
   - GDB debugging support
   - Hardware prefetcher model (absent from the original ZSim)
   - Network-on-Chip (NoC) model adapted from Damov's platform
   - **Multi-channel memory interface** — this is the primary contribution of the paper. The interface coupling between ZSim and the memory simulators (Ramulator, Ramulator2, DRAMsim3) is the subject of the paper's correctness analysis.

The interface implementation is the core novelty. The prefetcher, NoC, and GDB additions are supporting infrastructure that enable the experiments to run correctly on realistic configurations.

### 1.2. Relationship to Mess-ZSim

The `zsim-bsc/` tree closely tracks the Mess-ZSim version described in:

> Esmaili-Dokht, P., Sgherzi, F., Girelli, V. S., Boixaderas, I., Carmin, M., Monemi, A., Armejach, A., Mercadal, E., Llort, G., Radojković, P., Moreto, M., Giménez, J., Martorell, X., Ayguadé, E., Labarta, J., Confalonieri, E., Dubey, R., & Adlard, J. (2024). *A mess of memory system benchmarking, simulation and application profiling.* In Proceedings of the 57th IEEE/ACM International Symposium on Microarchitecture (MICRO) (pp. 136-152). IEEE. [https://mess.bsc.es/](https://mess.bsc.es/)

The snapshot committed here is the authoritative reference for this artifact and should be used as-is for reproduction.

---

## 2. Dependencies

This repository includes the simulator source trees, but not all runtime bundles by default. `./setup.sh` and `./scripts/setup-env.sh` will try to discover dependencies automatically and can download Pin/HDF5 bundles into `dependencies/` if they are missing.

### 2.1. Intel Pin — Critical Version Requirement

ZSim is a Pin-based dynamic binary instrumentation tool. **Pin 2.14 (build 71313) is required.** Earlier versions lack Sandy Bridge / Skylake support; later versions break the ZSim instrumentation interface. This is not a soft requirement.

> **Known compatibility issue:** Intel Pin 2.14 performs a Linux kernel version check that fails on kernel 4.0+, due to the version numbering change from 3.x to 4.x. If you are running a modern Linux kernel, Pin will refuse to start.
>
> **Workaround:** Pass the `-injection child` flag to the Pin invocation. This bypasses the version check and allows Pin 2.14 to function correctly on kernels 4.0 and above.

Set the environment variable:
```
PINPATH=<path to Pin 2.14 installation>
```

In this repository, `scripts/setup-env.sh` can auto-download Pin from its configured URL when `PINPATH` is not found.

### 2.2. HDF5

ZSim outputs simulation statistics in HDF5 format.

- **Required version used for the paper:** HDF5 1.8.16
- The `SConstruct` build file assumes HDF5 is installed system-wide, or pointed to by `HDF5_HOME`

Set the environment variable:
```
HDF5_HOME=<path to HDF5 installation>
```

If HDF5 headers/libs are available system-wide, `HDF5_HOME` may be left empty. `scripts/setup-env.sh` can auto-download an HDF5 bundle when needed.

### 2.3. Compiler

- **GCC 11.4.0** was used for the paper artifact runs
- The build system uses `scons`

### 2.4. Memory Simulator Paths

The memory simulator source trees in this directory also require environment variables so that ZSim can locate and link against them:

```
DRAMSIM3PATH=<path to dramsim3/DRAMsim3>
RAMULATORPATH=<path to ramulator>
RAMULATOR2PATH=<path to ramulator2>
```

### 2.5. Python (Post-processing)

Python 3 with the following packages is required for the plotting and result-processing pipeline:
- `pandas`
- `matplotlib`

---

## 3. Environment Setup

All environment variables above are tracked in a `.zsim-env` file at the repository root. This file must be created and sourced before building or running any experiment.

Recommended one-shot setup from repo root:
```bash
./setup.sh
```

Manual env generation only:
```bash
./scripts/setup-env.sh
source .zsim-env
```

To inspect the expected variables:
```bash
cat .zsim-env
```

To apply them to the current shell:
```bash
source .zsim-env
```

### Building ZSim

Once the environment is set up:
```bash
cd simulator-source/zsim-bsc
scons --r -j$(nproc)
```

For build variants:
- `--d`: Debug build
- `--o`: Optimized build (default)
- `--r`: Release build

---

## 4. Ramulator — Address Mapping Note

The Ramulator source tree (`ramulator/`) includes Intel Skylake-specific address mapping support, originally added to correctly model the multi-channel memory layout of Skylake platforms.

In the original implementation, enabling this required manually commenting and uncommenting two lines in `ramulator/src/memory.h`. To make this reproducible without source edits, the public artifact exposes this toggle through a dedicated config file:

```
ramulator/ramulator-configs/DDR4-config-MN4-skylake.cfg
```

The Figure 6a (address-mapping) experiment stage selects this config file, enabling Skylake-specific address mapping through configuration rather than source modification. This is the only interface-sensitive behavior in the artifact that was originally source-controlled; all other interface parameters are driven by `sb.cfg`.

---

## 5. Source Tree Notes

The simulator source trees are currently committed directly to this repository as file copies. In a future cleanup pass, they may be replaced by git submodules pinned to exact public commits. The `zsim-bsc/` tree is the only one that has been significantly extended beyond its upstream; the `dramsim3/`, `ramulator/`, and `ramulator2/` trees are kept minimally modified so that the changes remain clearly attributable and auditable.

For reference, the full artifact environment (simulator sources, benchmarks, and raw simulation outputs) requires approximately 24 GB of disk space.
