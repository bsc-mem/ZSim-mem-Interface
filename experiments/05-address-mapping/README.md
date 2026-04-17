# 05-address-mapping

This stage captures the Figure 6a experiment, which adds the Skylake-oriented physical address mapping on top of the corrected Figure 5 interface.

## Paper Figure

This stage corresponds to Figure 6a in the paper.

## Public Contents

- `sb.cfg`
  The experiment config used for this stage. It remains aligned with the Figure 5 setup, but points Ramulator at the Skylake address-mapping config.
- `processed/`
  The committed processed CSV used for comparisons and inspection.
- `figures/`
  The committed PDF and PNG figure outputs from the authoritative Figure 6a experiment drop.
- `raw-manifest.csv`
  Tracks the VM-hosted raw `bw-lat` artifact for this stage.

Use the shared experiment entrypoints in `../runner.sh`, `../run-one.sh`, and `../plot.py`.

## Intended Claim

This stage isolates the effect of the Intel Skylake address mapping after the interface timing model has already been corrected. Relative to Figure 5, the public `sb.cfg` stays functionally the same and the stage difference comes from Ramulator's address decomposition and hashing.

## Reproduction Note

The authoritative Figure 6a source drop carries the same top-level `sb.cfg` shape as Figure 5. The artifact exposes the actual stage change through `../../simulator-source/ramulator/ramulator-configs/DDR4-config-MN4-skylake.cfg`, which enables `skylake_address_mapping = on` in the shared Ramulator source tree.

## Raw Data Source

- processed public outputs: committed under `processed/` and `figures/`
- mirrored external raw input: `bw-lat/`
- raw artifact URLs and checksums: tracked in `raw-manifest.csv`
