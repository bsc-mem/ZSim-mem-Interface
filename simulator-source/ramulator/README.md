# Ramulator Source

This directory contains the Ramulator source tree used by the ZSim interface experiments in this artifact.

It is kept as a sibling shared source tree under `simulator-source/` so that:

- the experiment folders stay configuration-focused
- the public artifact keeps all simulator code in one repository
- the source can later be replaced by a pinned external branch or submodule if that becomes appropriate

The experiment folders do not duplicate this code. They reference it through shared configs and runner scripts.

The artifact currently uses two public Ramulator config variants for the main interface stages:

- `ramulator-configs/DDR4-config-MN4.cfg`
  Default mapping used by the corrected Figure 4 and Figure 5 stages.
- `ramulator-configs/DDR4-config-MN4-skylake.cfg`
  Enables `skylake_address_mapping = on` for the Figure 6a address-mapping stage.

This keeps the address-mapping experiment reproducible through configuration rather than by commenting and uncommenting code in `ramulator/Memory.h`.
