#!/usr/bin/env bash
# setup.sh — one-shot environment setup and build for the ZSim artifact.
#
# Run this once from the repository root on a Linux machine:
#   ./setup.sh
#
# Flags:
#   --rebuild   Clean and force-rebuild all memory simulators and ZSim
#
# What it does:
#   1. Checks system dependencies (GCC, scons, Python packages, libconfig++)
#   2. Generates .zsim-env (resolves all paths automatically, prompts only for Pin)
#   3. Builds memory simulators (Ramulator, DRAMsim3, Ramulator2)
#   4. Builds ZSim (release build)
#   5. Builds the benchmarks (ptr_chase and traffic_gen)

set -euo pipefail

REBUILD=false
for arg in "$@"; do
    case "$arg" in
        --rebuild) REBUILD=true ;;
        *) echo "Unknown argument: $arg"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$SCRIPT_DIR"

RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; BLD='\033[1m'; NC='\033[0m'
ok()   { echo -e "  ${GRN}✔${NC}  $*"; }
warn() { echo -e "  ${YLW}⚠${NC}  $*"; }
err()  { echo -e "  ${RED}✘${NC}  $*"; exit 1; }
step() { echo -e "\n${BLD}━━━  $*  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"; }

# ── 0. Platform check ─────────────────────────────────────────────────────────
if [[ "$(uname -s)" != "Linux" ]]; then
    err "This artifact requires Linux. Detected: $(uname -s)"
fi

step "Step 1 / 5 — Checking system dependencies"

# GCC
if command -v gcc &>/dev/null; then
    ok "GCC: $(gcc --version | head -1)"
else
    err "GCC not found. Install GCC 11: sudo apt install gcc g++"
fi

# scons
if command -v scons &>/dev/null; then
    ok "scons: $(scons --version 2>&1 | head -1)"
else
    err "scons not found. Install it: sudo apt install scons"
fi

# unzip (needed for dependency downloads)
if command -v unzip &>/dev/null; then
    ok "unzip: $(unzip -v 2>&1 | head -1)"
else
    err "unzip not found. Install it: sudo apt install unzip"
fi

# cmake (needed for DRAMsim3 and Ramulator2)
if command -v cmake &>/dev/null; then
    ok "cmake: $(cmake --version | head -1)"
else
    err "cmake not found. Install it: sudo apt install cmake"
fi

# libconfig++
if pkg-config --exists libconfig++ 2>/dev/null || ldconfig -p 2>/dev/null | grep -q libconfig++; then
    ok "libconfig++ found"
else
    warn "libconfig++ may be missing. If the ZSim build fails, run: sudo apt install libconfig++-dev"
fi

# Python + packages (required for plot.py)
if python3 -c "import pandas, matplotlib" 2>/dev/null; then
    ok "Python 3 with pandas and matplotlib"
else
    warn "pandas or matplotlib missing. Installing..."
    python3 -m pip install --user pandas matplotlib
    ok "pandas and matplotlib installed"
fi

# ── 1. Generate .zsim-env ─────────────────────────────────────────────────────
step "Step 2 / 5 — Generating .zsim-env"

"$REPO_ROOT/scripts/setup-env.sh"

if [[ ! -f "$REPO_ROOT/.zsim-env" ]]; then
    err ".zsim-env was not created. See scripts/setup-env.sh output above."
fi

# Source it for the remainder of this script
# shellcheck disable=SC1091
source "$REPO_ROOT/.zsim-env"

ok ".zsim-env sourced"

# ── 2. Build memory simulators ───────────────────────────────────────────────
step "Step 3 / 5 — Building memory simulators"

# Ramulator — make libramulator.so in ramulator/ramulator/
RAMULATOR_LIB="$RAMULATORPATH/ramulator/libramulator.so"
if [[ -f "$RAMULATORPATH/ramulator/libramulator.so" ]] && [[ "$REBUILD" == false ]]; then
    ok "libramulator.so already built"
else
    echo "  Building Ramulator..."
    [[ "$REBUILD" == true ]] && make -C "$RAMULATORPATH/ramulator" clean 2>/dev/null || true
    make -C "$RAMULATORPATH/ramulator" libramulator.so -j"$(nproc)" CXX=g++ CXXFLAGS='-DRAMULATOR -Wall -std=c++11 -w -O3 -D_GLIBCXX_USE_CXX11_ABI=0'
    [[ -f "$RAMULATORPATH/ramulator/libramulator.so" ]] && ok "libramulator.so built" || err "Ramulator build failed."
fi

# DRAMsim3 — cmake build (outputs libdramsim3.so to $DRAMSIM3PATH/, one level above build/)
DRAMSIM3_LIB="$DRAMSIM3PATH/libdramsim3.so"
if [[ -f "$DRAMSIM3_LIB" ]] && [[ "$REBUILD" == false ]]; then
    ok "libdramsim3.so already built"
else
    echo "  Building DRAMsim3..."
    [[ "$REBUILD" == true ]] && rm -rf "$DRAMSIM3PATH/build" || true
    cmake -S "$DRAMSIM3PATH" -B "$DRAMSIM3PATH/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=0" 2>&1 | tail -3
    make -C "$DRAMSIM3PATH/build" dramsim3 -j"$(nproc)"
    [[ -f "$DRAMSIM3_LIB" ]] && ok "libdramsim3.so built" || err "DRAMsim3 build failed."
fi

# Ramulator2 — cmake build (LIBRARY_OUTPUT_DIRECTORY = PROJECT_SOURCE_DIR, so lib lands in $RAMULATOR2PATH/)
RAMULATOR2_LIB="$RAMULATOR2PATH/libramulator2.so"
if [[ -f "$RAMULATOR2_LIB" ]] && [[ "$REBUILD" == false ]]; then
    ok "libramulator2.so already built"
else
    echo "  Building Ramulator2 from: $RAMULATOR2PATH"
    [[ "$REBUILD" == true ]] && rm -rf "$RAMULATOR2PATH/build" || true
    cmake -S "$RAMULATOR2PATH" -B "$RAMULATOR2PATH/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=0"
    make -C "$RAMULATOR2PATH/build" ramulator -j"$(nproc)"
    [[ -f "$RAMULATOR2_LIB" ]] && ok "libramulator2.so built" || err "Ramulator2 build failed. Expected: $RAMULATOR2_LIB"
fi

# ── 3. Build ZSim ─────────────────────────────────────────────────────────────
step "Step 4 / 5 — Building ZSim (release)"

ZSIM_DIR="$REPO_ROOT/simulator-source/zsim-bsc"
ZSIM_BIN="$ZSIM_DIR/build/release/zsim"

if [[ -x "$ZSIM_BIN" ]] && [[ "$REBUILD" == false ]]; then
    ok "ZSim release binary already exists: $ZSIM_BIN"
    echo "    To force a rebuild, run: ./setup.sh --rebuild"
else
    echo "  Building ZSim with $(nproc) parallel jobs. This may take several minutes..."
    (
        cd "$ZSIM_DIR"
        [[ "$REBUILD" == true ]] && scons -c
        scons --r -j"$(nproc)"
    )
    if [[ -x "$ZSIM_BIN" ]]; then
        ok "ZSim built: $ZSIM_BIN"
    else
        err "ZSim build failed. Check the output above."
    fi
fi

# ── 3. Build benchmarks ───────────────────────────────────────────────────────
step "Step 5 / 5 — Building benchmarks"

"$REPO_ROOT/scripts/build-benchmarks.sh"

PTR_CHASE="$REPO_ROOT/benchmarks/ptr_chase/ptr_chase"
TRAFFIC_GEN="$REPO_ROOT/benchmarks/traffic_gen/traffic_gen.x"

[[ -x "$PTR_CHASE" ]]   && ok "ptr_chase built: $PTR_CHASE"   || err "ptr_chase build failed."
[[ -x "$TRAFFIC_GEN" ]] && ok "traffic_gen built: $TRAFFIC_GEN" || err "traffic_gen build failed."

# ── Done ──────────────────────────────────────────────────────────────────────
echo ""
echo -e "${GRN}${BLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${GRN}${BLD}  Setup complete. The artifact is ready to run.${NC}"
echo -e "${GRN}${BLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
echo "  To run an experiment:"
echo "    source .zsim-env"
echo "    ./experiments/runner.sh 01-baseline"
echo ""
echo "  To compare committed results (no simulation needed):"
echo "    ./scripts/compare-results.sh 01-baseline 04-model-correct"
echo ""
