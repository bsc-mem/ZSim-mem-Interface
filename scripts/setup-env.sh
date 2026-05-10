#!/usr/bin/env bash
# setup-env.sh — generate .zsim-env at the repository root.
#
# The three memory-simulator paths are always inside this repo, so they are
# resolved automatically.  PINPATH and HDF5_HOME are located by searching
# common system paths; if not found they are downloaded automatically.
#
# Usage (from any directory inside the repo):
#   ./scripts/setup-env.sh
#   source .zsim-env        # or let the script source it for the current shell

set -euo pipefail

# ── Dependency download URLs ──────────────────────────────────────────────────
# Update these when the hosting location changes.
PIN_DOWNLOAD_URL="https://zenodo.org/records/19629352/files/pin.tar.gz?download=1"
HDF5_DOWNLOAD_URL="https://zenodo.org/records/19629352/files/hdf5.tar.gz?download=1"
PIN_SHA256="290346631b7a79f99aacca891176fb4ce4a574f614a1dfcde7e2d325f83a9603"
HDF5_SHA256="c5e5105facec8b14d24fc530b64ef32eba54052f4e242fba1199ca17141bd12e"
# ─────────────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
ENV_FILE="$REPO_ROOT/.zsim-env"

# ── colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GRN='\033[0;32m'; YLW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "  ${GRN}✔${NC}  $*"; }
warn() { echo -e "  ${YLW}⚠${NC}  $*"; }
err()  { echo -e "  ${RED}✘${NC}  $*"; }

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  ZSim environment setup"
echo "  Repository root: $REPO_ROOT"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# ── helpers ───────────────────────────────────────────────────────────────────

# Search common prefixes for a file/dir matching a pattern; return the first hit.
find_first() {
    local pattern="$1"; shift
    local search_roots=("$@")
    for root in "${search_roots[@]}"; do
        [[ -d "$root" ]] || continue
        local hit
        hit=$(find "$root" -maxdepth 6 -name "$pattern" 2>/dev/null | head -1)
        if [[ -n "$hit" ]]; then
            printf '%s\n' "$hit"
            return 0
        fi
    done
    return 1
}

# Download a file given its URL, extract it into dest_dir,
# and print the path of the extracted top-level directory.
download_and_extract() {
    local url="$1"
    local dest_dir="$2"
    local label="$3"
    local expected_sha256="$4"

    mkdir -p "$dest_dir"
    local archive="$dest_dir/${label}.download"

    echo "    Downloading $label from Zenodo..." >&2
    curl -s -L -o "$archive" "$url"

    # Validate SHA256 if provided
    if [[ -n "$expected_sha256" ]]; then
        local actual_sha256
        actual_sha256=$(sha256sum "$archive" | awk '{print $1}')
        if [[ "$actual_sha256" != "$expected_sha256" ]]; then
            rm -f "$archive"
            echo -e "  ${RED}✘${NC}  SHA256 checksum mismatch for $label." >&2
            echo -e "  ${RED}✘${NC}  Expected: $expected_sha256" >&2
            echo -e "  ${RED}✘${NC}  Actual:   $actual_sha256" >&2
            exit 1
        fi
        ok "$label SHA256 verified"
    fi

    echo "    Extracting $label..." >&2

    extract_archive() {
        local f="$1" d="$2"
        # Zip: check magic bytes (PK header) first — most reliable
        if python3 -c "
import zipfile, sys
sys.exit(0 if zipfile.is_zipfile(sys.argv[1]) else 1)
" "$f" 2>/dev/null; then
            unzip -q "$f" -d "$d"
        # gzip / tar.gz / tar.bz2 / tar.xz / plain tar
        elif tar -tzf "$f" &>/dev/null 2>&1; then
            tar -xzf "$f" -C "$d"
        elif tar -tjf "$f" &>/dev/null 2>&1; then
            tar -xjf "$f" -C "$d"
        elif tar -tJf "$f" &>/dev/null 2>&1; then
            tar -xJf "$f" -C "$d"
        elif tar -tf  "$f" &>/dev/null 2>&1; then
            tar -xf  "$f" -C "$d"
        else
            return 1
        fi
    }

    if ! extract_archive "$archive" "$dest_dir"; then
        echo -e "  ${RED}✘${NC}  Could not extract $label." >&2
        exit 1
    fi
    rm -f "$archive"

    # Print the first directory that appeared — only this goes to stdout
    find "$dest_dir" -mindepth 1 -maxdepth 1 -type d | sort | head -1
}

# ── 1. Memory simulator paths (always inside the repo) ───────────────────────

DRAMSIM3PATH="$REPO_ROOT/simulator-source/dramsim3/DRAMsim3"
RAMULATORPATH="$REPO_ROOT/simulator-source/ramulator"
RAMULATOR2PATH="$REPO_ROOT/simulator-source/ramulator2"
DRAMSYSPATH="$REPO_ROOT/simulator-source/DRAMSys"

for var_name in DRAMSIM3PATH RAMULATORPATH RAMULATOR2PATH DRAMSYSPATH; do
    path="${!var_name}"
    if [[ -d "$path" ]]; then
        ok "$var_name = $path"
    else
        err "$var_name directory not found: $path"
        err "The simulator source trees appear to be missing. Check the repo."
        exit 1
    fi
done

echo ""

# ── 2. PINPATH ────────────────────────────────────────────────────────────────

SEARCH_ROOTS=(/opt /usr/local /home "$HOME" /tools /scratch /software)

PINPATH=""

# Helper: check if a directory looks like a valid Pin root
is_pin_root() {
    local d="$1"
    [[ -d "$d" ]] || return 1
    [[ -f "$d/source/include/pin.H" || -f "$d/source/include/pin/pin.H" ]] && return 0
    return 1
}

# 1. Check the local dependencies/ folder first (previously downloaded)
for candidate in "$REPO_ROOT/dependencies"/pin*; do
    if is_pin_root "$candidate"; then
        PINPATH="$candidate"
        break
    fi
done

# 2. Search common system locations
if [[ -z "$PINPATH" ]]; then
    for root in "${SEARCH_ROOTS[@]}"; do
        [[ -d "$root" ]] || continue
        while IFS= read -r candidate; do
            if is_pin_root "$candidate"; then
                PINPATH="$candidate"
                break 2
            fi
        done < <(find "$root" -maxdepth 5 \( -name "pin-2.14*" -o -name "pin" \) -type d 2>/dev/null)
    done
fi

if [[ -n "$PINPATH" && -d "$PINPATH" ]]; then
    ok "PINPATH = $PINPATH"
else
    warn "Pin 2.14 not found on system — downloading automatically..."
    DEPS_DIR="$REPO_ROOT/dependencies"
    PINPATH=$(download_and_extract "$PIN_DOWNLOAD_URL" "$DEPS_DIR" "pin" "$PIN_SHA256")
    if [[ -z "$PINPATH" || ! -d "$PINPATH" ]]; then
        echo -e "  ${RED}✘${NC}  Pin download or extraction failed." >&2
        echo -e "  ${RED}✘${NC}  Download manually from: $PIN_DOWNLOAD_URL" >&2
        echo -e "  ${RED}✘${NC}  Extract it and set PINPATH in .zsim-env manually." >&2
        exit 1
    fi
    ok "PINPATH = $PINPATH  (downloaded)"
fi

echo ""

# ── 3. HDF5_HOME ──────────────────────────────────────────────────────────────

HDF5_HOME=""

# Helper: check if a directory looks like a valid HDF5 root
is_hdf5_root() {
    local d="$1"
    [[ -d "$d" ]] && [[ -f "$d/include/hdf5.h" ]] && [[ -d "$d/lib" ]] && return 0
    return 1
}

# 1. Check the local dependencies/ folder first (previously downloaded)
for candidate in "$REPO_ROOT/dependencies"/hdf5*; do
    if is_hdf5_root "$candidate"; then
        HDF5_HOME="$candidate"
        ok "HDF5_HOME = $HDF5_HOME  (local dependencies/)"
        break
    fi
done

# 2. Check system-wide
if [[ -z "$HDF5_HOME" ]]; then
    for sys_inc in /usr/include /usr/local/include; do
        if [[ -f "$sys_inc/hdf5.h" ]]; then
            HDF5_HOME=""
            ok "HDF5 found system-wide (no HDF5_HOME needed)"
            break
        fi
    done
fi

# 3. Search common system locations
if [[ -z "$HDF5_HOME" ]]; then
    for root in "${SEARCH_ROOTS[@]}"; do
        [[ -d "$root" ]] || continue
        while IFS= read -r hdr; do
            inc_dir="$(dirname "$hdr")"
            candidate="$(cd "$inc_dir/.." && pwd)"
            if is_hdf5_root "$candidate"; then
                HDF5_HOME="$candidate"
                ok "HDF5_HOME = $HDF5_HOME"
                break 2
            fi
        done < <(find "$root" -maxdepth 6 -name "hdf5.h" 2>/dev/null)
    done
fi

if [[ -z "$HDF5_HOME" ]]; then
    warn "HDF5 not found on system — downloading automatically..."
    DEPS_DIR="$REPO_ROOT/dependencies"
    HDF5_HOME=$(download_and_extract "$HDF5_DOWNLOAD_URL" "$DEPS_DIR" "hdf5" "$HDF5_SHA256")
    if [[ -z "$HDF5_HOME" || ! -d "$HDF5_HOME" ]]; then
        echo -e "  ${RED}✘${NC}  HDF5 download or extraction failed." >&2
        echo -e "  ${RED}✘${NC}  Download manually from: $HDF5_DOWNLOAD_URL" >&2
        echo -e "  ${RED}✘${NC}  Extract it and set HDF5_HOME in .zsim-env manually." >&2
        exit 1
    fi
    ok "HDF5_HOME = $HDF5_HOME  (downloaded)"
fi

echo ""

# ── 4. Write .zsim-env ───────────────────────────────────────────────────────

cat > "$ENV_FILE" <<EOF
# .zsim-env — generated by scripts/setup-env.sh on $(date)
# Source this file before building or running any experiment:
#   source .zsim-env

export PINPATH="$PINPATH"
export HDF5_HOME="$HDF5_HOME"
export DRAMSIM3PATH="$DRAMSIM3PATH"
export RAMULATORPATH="$RAMULATORPATH"
export RAMULATOR2PATH="$RAMULATOR2PATH"
export DRAMSYSPATH="$DRAMSYSPATH"
EOF

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ok ".zsim-env written to $ENV_FILE"
echo ""
echo "  Next steps:"
echo "    source .zsim-env"
echo "    cd simulator-source/zsim-bsc && scons -j\$(nproc)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
