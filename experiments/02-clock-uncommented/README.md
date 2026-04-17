# 02-clock-uncommented

This stage captures the intermediate experiment where the original Ramulator interface runs with its clock-divider path enabled, before the later full clock-correct interface fix.

## Paper Figure

This stage corresponds to Figure 3 in the paper.

## Public Contents

- `sb.cfg`
  Uses the shared source tree and enables `ramulatorOrgUseClockDivider = true`.
- `processed/`
  The committed processed CSV used for comparisons and inspection.
- `figures/`
  The committed PDF and PNG figure outputs for this stage.
- `raw-manifest.csv`
  Tracks the VM-hosted raw `bw-lat` artifact for this stage.

Use the shared experiment entrypoints in `../runner.sh`, `../run-one.sh`, and `../plot.py`.

## Intended Claim

This stage isolates the effect of enabling the original host-to-memory clock divider in the pre-correction interface path. In the paper sequence this is the intermediate step for Figure 3.

## Reproduction Note

The authoritative Figure 3 drop adds the `100000` pause point on top of the Figure 2 sweep. In this simplified artifact flow, `runner.sh` now uses the shared built-in sweep for all runnable stages.

## Raw Data Source

- processed public outputs: committed under `processed/` and `figures/`
- mirrored external raw input: `bw-lat/`
- raw artifact URLs and checksums: tracked in `raw-manifest.csv`
