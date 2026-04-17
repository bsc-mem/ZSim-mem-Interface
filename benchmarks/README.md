# Benchmarks Overview

This directory contains the shared workloads used across the public artifact to generate Bandwidth-Latency curves. These benchmarks are based on the methodology described in the Mess framework.

> Esmaili-Dokht, P., Sgherzi, F., Girelli, V. S., Boixaderas, I., Carmin, M., Monemi, A., Armejach, A., Mercadal, E., Llort, G., Radojković, P., Moreto, M., Giménez, J., Martorell, X., Ayguadé, E., Labarta, J., Confalonieri, E., Dubey, R., & Adlard, J. (2024). *A mess of memory system benchmarking, simulation and application profiling.* In Proceedings of the 57th IEEE/ACM International Symposium on Microarchitecture (MICRO) (pp. 136-152). IEEE. [https://mess.bsc.es/](https://mess.bsc.es/)

## Workloads

### `ptr_chase/`
Pointer-chasing latency benchmark designed to estimate memory access latency. This workload generates a sequence of dependent memory accesses that minimize hardware prefetching effects, providing a clean measurement of the memory system's latency characteristics.

### `traffic_gen/`
Traffic-generator benchmark used to sweep read ratio and pause values while producing bandwidth pressure. The implementation is based on a modified STREAM benchmark that systematically varies memory access patterns to characterize the bandwidth-latency trade-offs in the memory system.

## Design Philosophy

These benchmarks are intentionally shared across all experiments to ensure consistency and comparability of results. The experiment folders do not duplicate workload source code; instead, they reference these shared implementations while varying only the simulator configuration and interface parameters.

For detailed methodology and experimental design considerations, please refer to the Mess framework documentation and the associated MICRO 2024 paper.
