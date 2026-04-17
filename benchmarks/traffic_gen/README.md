# Traffic Generator Benchmark

Memory bandwidth load benchmark based on a modified STREAM implementation, designed to generate controlled memory traffic with configurable read/write ratios and bandwidth pressure.

## Overview

The traffic generator benchmark creates sustained memory traffic towards main memory with two key runtime parameters:

1. **Read ratio** (50-100% in 2% increments): Controls the proportion of read requests in total traffic
2. **Pause value**: Injects delays between memory requests to modulate generated bandwidth

This benchmark extends the original STREAM benchmark [[1]()] with significant modifications for memory system characterization. It uses MPI for parallel execution and optional OpenMP directives for threading.

## Key Modifications from STREAM

The benchmark differs from the original STREAM benchmark in several important ways:

- **Copy kernel only**: Implements only the memory copy operation with custom assembly kernels
- **Assembly optimization**: Read/write kernels coded in x86 assembly using AVX instructions and non-temporal stores
- **No result verification**: Array contents are not checked at completion
- **No internal timing**: All timing removed as external measurement tools are used
- **Indefinite execution**: Runs continuously rather than for a fixed number of iterations

## Usage

### Compilation

Configure the desired compiler and flags in the Makefile. The code has been tested with:
- **gcc** with **OpenMPI** library
- **Intel** compiler with **Intel MPI** library

Optional: Set array size via compiler flag `-DTRAFFIC_GENERATOR_ARRAY_SIZE=...`

To compile:
```bash
make
```

### Execution

Example execution with 16 MPI processes, 64% read traffic, and pause value of 1000:

```bash
mpirun -n 16 ./traffic_gen.x -r 64 -p 1000
```

Parameters:
- `-r`: Read ratio (50-100, step 2)
- `-p`: Pause value (0 for maximum bandwidth)

Sample output:
```
-------------------------------------------------------------
$ Memory bandwidth load kernel $
-------------------------------------------------------------
This system uses 8 bytes per array element.
-------------------------------------------------------------
Total Aggregate Array size = 80000000 (elements)
Total Aggregate Memory per array = 610.4 MiB (= 0.6 GiB).
Total Aggregate memory required = 1220.7 MiB (= 1.2 GiB).
Data is distributed across 16 MPI ranks
   Array size per MPI rank = 5000000 (elements)
   Memory per array per MPI rank = 38.1 MiB (= 0.0 GiB).
   Total memory per MPI rank = 76.3 MiB (= 0.1 GiB).
-------------------------------------------------------------
The kernel will be executed indefinitely.
```

### Performance Measurement

For optimal precision, measure memory bandwidth using uncore counters with tools such as **perf** or **LIKWID**. This approach was used to characterize bandwidth-latency dependencies in the PROFET model.

## Implementation Details

### Copy Kernel Architecture

The copy kernel is implemented in x86 assembly to perform controlled loads and stores between independent memory arrays. Each read ratio has a dedicated kernel function in **utils.c**, containing exactly 100 vector instructions with the appropriate load/store ratio.

For example, kernel `TRAFFIC_GENERATOR_copy_60` (60% reads) contains:
- 60 vector load instructions
- 40 vector store instructions

### Assembly Implementation

The code uses AT&T syntax (GNU tools convention). Key addressing format:
- `$200`: Immediate value
- `%r10`: Register
- `32(%r10,%rbx,8)`: Memory address = `%r10 + %rbx*8 + 32`

Instructions:
- **Loads**: `vmovupd` - Loads 256-bit (32B) from memory to vector register `%ymm0`
- **Stores**: `vmovntpd` - Stores 256-bit (32B) from `%ymm1` to memory with non-temporal hint

Non-temporal stores bypass cache hierarchy to ensure memory traffic reaches main memory.

### Pause Mechanism

Load and store instruction blocks are interleaved with calls to the `nop` function (implemented in **nop.c**). This function executes the specified number of NOP instructions passed via the `rdi` register, providing precise control over request timing.

## References

\[1\] *John D. McCalpin. 1991-2007. STREAM: Sustainable Memory Bandwidth in High Performance Computers. Technical Report. University of Virginia. http://www.cs.virginia.edu/stream/*

[1]: http://www.cs.virginia.edu/stream/ "Original STREAM benchmark developed by John D. McCalpin"
