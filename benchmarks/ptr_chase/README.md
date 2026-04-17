# Pointer Chase Benchmark

Memory access latency benchmark designed to measure access latency across different levels of the memory hierarchy using randomized memory access patterns that minimize hardware prefetching effects.

## Overview

The pointer chase benchmark implements a classic pointer-chasing pattern using x86 assembly load instructions. By varying the load file size, researchers can target specific levels of the memory hierarchy (L1, L2, L3 cache, main memory) and isolate their latency characteristics.

The benchmark generates a randomized access pattern that effectively defeats prefetchers, allowing for clean measurement of memory access latency. Execution time or CPU cycles can be measured using performance monitoring tools such as **perf** or **LIKWID**.

## Methodology

To achieve precise latency measurements, the benchmark employs several techniques:

1. **Randomized access patterns** minimize prefetcher effects
2. **Page walk and DTLB penalty measurement** allows subtraction of address translation overhead
3. **Per-instruction latency calculation** by dividing corrected cycles by the number of load instructions

The final result provides per-load access latency for the targeted memory hierarchy level, excluding address translation overhead.

## Usage

### Compilation

Installation requires a **C** compiler. The code has been tested with **gcc** (version 4.9.1 or higher) and the **Intel** compiler.

Key configuration parameters in the Makefile:
- `CC` and `CFLAGS`: Compiler and compilation flags
- `sizes`: Load file sizes in kB (e.g., `[524288]` for 512MB)
- `ins`: Number of load instructions

To compile the benchmark:
```bash
make
```

This process:
1. Compiles the load file generator binary
2. Creates a randomized access pattern load file
3. Produces the pointer-chasing binary

### Execution Example

The following example demonstrates execution on a Sandy Bridge E5-2670 platform with performance monitoring:

```bash
numactl -C 0 -m 0 perf stat -e cycles:u,instructions:u,r1008:u,r0408:u ./ptr_chase
```

This command:
- Executes on CPU core 0 with memory node 0 binding
- Measures cycles, instructions, secondary DTLB hits, and page walks
- Allows calculation of pure memory access latency by subtracting address translation overhead

## Technical Notes

The benchmark's accuracy depends on proper accounting for system-specific overhead. On the tested platform, secondary DTLB penalties of 7 cycles were subtracted from total cycles before dividing by the 5 million load instructions to obtain the final latency measurement.
