# 08-portability-ramulator2

This stage captures the final portability experiment that swaps the memory backend from Ramulator to Ramulator2 while keeping the same front-end platform refinements.

## Paper Figure

This stage corresponds to Figure 7a in the paper.

## Public Contents

- `sb.cfg`
  The Ramulator2-backed config used for this stage. It keeps the final platform refinements, including NoC and prefetcher, and switches the memory simulator backend to Ramulator2.
- `processed/`
  The committed processed CSV used for comparisons and inspection.
- `figures/`
  The committed PDF and PNG figure outputs from the authoritative Ramulator2 portability drop.
- `raw-manifest.csv`
  Tracks the VM-hosted raw `bw-lat` artifact for this stage.

Use the shared experiment entrypoints in `../runner.sh`, `../run-one.sh`, and `../plot.py`.

## Intended Claim

This stage shows that the corrected interface approach is not limited to the original Ramulator backend and can be reproduced with Ramulator2 on the same final platform model.

## Reproduction Note

The committed portability drop contains the application and interface processed views used in the paper. Those are the public source of truth for Figure 7a in this artifact. The shared `../plot.py` treats the backend-side memory view as optional here and will skip it when the corresponding stats are not available, which matches the Ramulator2-specific plotter that Pouya used for the authoritative drop.

## Raw Data Source

- processed public outputs: committed under `processed/` and `figures/`
- mirrored external raw input: `bw-lat/`
- raw artifact URLs and checksums: tracked in `raw-manifest.csv`
