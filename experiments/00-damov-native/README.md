# 00-system-agnostic

This experiment preserves the existing system-agnostic seed that was already present in the repository. It is useful as a structural starting point for the public artifact, but it is not the main paper-result path.

## Paper Figure

This stage is not currently tied to a main paper figure. It is kept as a system-agnostic structural seed.

## Current Contents

- original config files
- shared benchmark binaries are expected from `../../benchmarks/`
- shared experiment entrypoints are available in `../runner.sh`, `../run-one.sh`, and `../plot.py`
- DAMOV native simulator builds use `.zsim-env` for shared `PINPATH` and `RAMULATORPATH`
- `raw-manifest.csv` tracks the current raw-data publication status for this stage

## DAMOV Source Changes

The DAMOV simulator logic is intentionally kept unchanged for this artifact. The local updates are limited to build and portability plumbing: the SCons scripts were updated to run with Python 3, syscall handling was refreshed for newer Ubuntu releases, and DAMOV now reuses the root project dependencies resolved by `setup.sh` (`PINPATH` and `RAMULATORPATH`) instead of duplicating Pin and Ramulator source trees under `damov-src`.

## Status

- public structure: populated
- committed processed paper outputs: not yet added
- raw artifact manifest: URLs pending (we'll add the URLs for the raw results shortly)

## Raw Data Source

- committed processed outputs: not yet added
- raw artifact publication status: tracked in `raw-manifest.csv`
