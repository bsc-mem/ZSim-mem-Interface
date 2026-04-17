# 01-baseline

This experiment captures the baseline ZSim plus original Ramulator interface configuration.

## Paper Figure

This stage corresponds to Figure 2 in the paper.

## Public Contents

- `sb.cfg`
  The baseline config used for this stage.
- `processed/`
  The committed processed CSV used for comparisons and inspection.
- `figures/`
  The committed PDF and PNG figure outputs for this experiment.
- `raw-manifest.csv`
  Tracks the VM-hosted raw `bw-lat` artifact for this stage.

Use the shared experiment entrypoints in `../runner.sh`, `../run-one.sh`, and `../plot.py`.

## Intended Claim

This stage is the reference point for the later corrected-interface comparison.

## Raw Data Source

- processed public outputs: committed under `processed/` and `figures/`
- mirrored external raw input: `bw-lat/`
- raw artifact URLs and checksums: tracked in `raw-manifest.csv`
