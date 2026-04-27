#!/usr/bin/env bash
set -euo pipefail

SCRIPT_REAL="$(python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_REAL")" && pwd -P)"
SIMULATOR_DIR="$(cd "$SCRIPT_DIR/.." && pwd -P)"
EXPERIMENT_DIR="$(cd "$SIMULATOR_DIR/../.." && pwd -P)"
REPO_ROOT="$(cd "$EXPERIMENT_DIR/../.." && pwd -P)"
ENV_FILE="$REPO_ROOT/.zsim-env"

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing $ENV_FILE; generating it with scripts/setup-env.sh..."
  "$REPO_ROOT/scripts/setup-env.sh"
fi

# shellcheck disable=SC1090
source "$ENV_FILE"

: "${PINPATH:?PINPATH is not set in .zsim-env}"
: "${RAMULATORPATH:?RAMULATORPATH is not set in .zsim-env}"

if [[ ! -d "$PINPATH" ]]; then
  echo "PINPATH does not exist: $PINPATH" >&2
  exit 1
fi

if [[ ! -d "$RAMULATORPATH" ]]; then
  echo "RAMULATORPATH does not exist: $RAMULATORPATH" >&2
  exit 1
fi

resolve_ramulator_build_dir() {
  local base="$1"
  if [[ -f "$base/Makefile" ]] && [[ -f "$base/RamulatorWrapper.h" ]]; then
    printf '%s\n' "$base"
    return 0
  fi
  if [[ -f "$base/ramulator/Makefile" ]] && [[ -f "$base/ramulator/RamulatorWrapper.h" ]]; then
    printf '%s\n' "$base/ramulator"
    return 0
  fi
  return 1
}

RAMULATOR_BUILD_DIR="$(resolve_ramulator_build_dir "$RAMULATORPATH" || true)"
if [[ -z "$RAMULATOR_BUILD_DIR" ]]; then
  echo "Could not resolve Ramulator build dir from RAMULATORPATH=$RAMULATORPATH" >&2
  exit 1
fi

NUMCPUS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN || echo 1)"
MODE="${1:-all}"
LIBCONFIGPATH="$SIMULATOR_DIR/libconfig"
BUILD_DIR="${DAMOV_BUILD_DIR:-build/}"

ensure_build_dir_writable() {
  local target="$SIMULATOR_DIR/$BUILD_DIR"
  if [[ -e "$target" && ! -w "$target" ]]; then
    echo "DAMOV build directory is not writable: $target" >&2
    echo "Remove or chown the stale build directory, or set DAMOV_BUILD_DIR to a writable path." >&2
    exit 1
  fi
}

build_local_libconfig() {
  if [[ ! -x "$LIBCONFIGPATH/configure" ]]; then
    echo "Missing local libconfig configure script: $LIBCONFIGPATH/configure" >&2
    exit 1
  fi
  local libconfig_ready=false
  if [[ -f "$LIBCONFIGPATH/include/libconfig.h" ]] && compgen -G "$LIBCONFIGPATH/lib/libconfig++.so*" >/dev/null; then
    libconfig_ready=true
  fi
  (
    cd "$LIBCONFIGPATH"
    if [[ ! -f "$LIBCONFIGPATH/Makefile" ]]; then
      ./configure --prefix="$LIBCONFIGPATH"
    fi
    make -j"$NUMCPUS"
    make install
  ) || {
    if [[ "$libconfig_ready" == true ]]; then
      echo "Warning: local libconfig refresh failed; reusing existing installed libconfig under $LIBCONFIGPATH" >&2
      return 0
    fi
    return 1
  }
}

build_ramulator() {
  make -C "$RAMULATOR_BUILD_DIR" libramulator.so -j"$NUMCPUS"
}

case "$MODE" in
  z)
    echo "Compiling only DAMOV ZSim ..."
    ensure_build_dir_writable
    export PINPATH RAMULATORPATH LIBCONFIGPATH
    (
      cd "$SIMULATOR_DIR"
      scons --buildDir="$BUILD_DIR" -j"$NUMCPUS"
    )
    ;;
  r)
    echo "Compiling only shared Ramulator ..."
    build_ramulator
    ;;
  all)
    echo "Compiling all (local libconfig + shared Ramulator + DAMOV ZSim) ..."
    build_local_libconfig
    build_ramulator
    ensure_build_dir_writable
    export PINPATH RAMULATORPATH LIBCONFIGPATH
    (
      cd "$SIMULATOR_DIR"
      scons --r --buildDir="$BUILD_DIR" -j"$NUMCPUS"
    )
    ;;
  *)
    echo "Unknown mode '$MODE'. Expected one of: all, z, r" >&2
    exit 1
    ;;
esac
