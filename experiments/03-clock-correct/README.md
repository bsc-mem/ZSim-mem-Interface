# 03-clock-correct

This stage captures the first shared-interface experiment after moving away from the original `RamulatorOrg` path. It uses the newer `Ramulator` interface with the fixed latency estimator, before the later moving-average model correction.

## Paper Figure

This stage corresponds to Figure 4 in the paper.

## Public Contents

- `sb.cfg`
  Uses the shared `Ramulator` interface with `boundPhaseLatencyEstimator = "fix"`.
- `processed/`
  The committed processed CSV used for comparisons and inspection.
- `figures/`
  The committed PDF and PNG figure outputs for this stage.
- `raw-manifest.csv`
  Tracks the VM-hosted raw `bw-lat` artifact for this stage.

Use the shared experiment entrypoints in `../runner.sh`, `../run-one.sh`, and `../plot.py`.

## Intended Claim

This stage isolates the interface-side clock-scaling correction while keeping the older fixed-response latency model. It is the direct precursor to the model-correct Figure 5 stage.

## Raw Data Source

- processed public outputs: committed under `processed/` and `figures/`
- mirrored external raw input: `bw-lat/`
- raw artifact URLs and checksums: tracked in `raw-manifest.csv`
