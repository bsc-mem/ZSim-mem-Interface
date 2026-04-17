# 04-model-correct

This experiment captures the corrected interface stage before the later minor refinements such as address mapping, NoC, and prefetcher updates.

## Paper Figure

This stage corresponds to Figure 5 in the paper.

## Public Contents

- `sb.cfg`
  The config used for the model-correct stage, with `boundPhaseLatencyEstimator = "mavg"`.
- `processed/`
  The committed processed CSV used for comparisons and inspection.
- `figures/`
  The committed PDF and PNG figure outputs for this experiment.
- `raw-manifest.csv`
  Tracks the VM-hosted raw `bw-lat` artifact for this stage.

Use the shared experiment entrypoints in `../runner.sh`, `../run-one.sh`, and `../plot.py`.

## Intended Claim

This stage is the main comparison target against the baseline for the interface correction. Relative to Figure 4, it switches from the fixed estimator to the moving-average latency estimator used for the final interface model.

## Raw Data Source

- processed public outputs: committed under `processed/` and `figures/`
- mirrored external raw input: `bw-lat/`
- raw artifact URLs and checksums: tracked in `raw-manifest.csv`
