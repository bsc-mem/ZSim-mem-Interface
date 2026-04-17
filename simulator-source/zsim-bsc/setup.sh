#!/usr/bin/env bash
set -euo pipefail

CONFIG_FILE=".zsim-env"
WRAPPER_FILE="scons.sh"

bold() { printf "\e[1m%s\e[0m\n" "$*"; }
info() { printf "%s\n" "$*"; }
warn() { printf "\e[33m%s\e[0m\n" "$*"; }
err()  { printf "\e[31m%s\e[0m\n" "$*"; }

###### helpers 
ask_path() {
  # ask_path VAR "Description" REQUIRED[Y/N] [default]
  local var="$1" desc="$2" required="$3" default="${4:-}"
  local value=""
  while :; do
    if [[ -n "$default" ]]; then
      read -r -e -p "$(printf "%s [%s]: " "$desc" "$default")" value || true
      value="${value:-$default}"
    else
      read -r -e -p "$(printf "%s: " "$desc")" value || true
    fi
    if [[ -z "${value}" ]]; then
      if [[ "$required" == "Y" ]]; then
        warn "This is required."
        continue
      else
        break
      fi
    fi
    if [[ -d "${value}" ]]; then
      break
    else
      warn "Not a directory: ${value}"
      [[ "$required" == "Y" ]] || break
    fi
  done
  printf -v "$var" '%s' "${value}"
}

path_has_any() {
  # path_has_any /base "rel1 rel2 ..." -> return 0 if any exists
  local base="$1"; shift
  IFS=' ' read -r -a rels <<< "$*"
  for r in "${rels[@]}"; do
    [[ -e "${base}/${r}" ]] && return 0
  done
  return 1
}

confirm() {
  local msg="$1" default="${2:-Y}" prompt="[y/N]"
  [[ "$default" =~ ^[Yy]$ ]] && prompt="[Y/n]"
  local ans; read -r -p "$msg $prompt " ans || true
  ans="${ans:-$default}"
  [[ "$ans" =~ ^[Yy]$ ]]
}

write_kv() {
  local key="$1" val="${2:-}"
  if [[ -n "$val" ]]; then
    printf 'export %s=%q\n' "$key" "$val" >> "$CONFIG_FILE"
  fi
}

path_add_ld() {
  local dir="$1"
  [[ -d "$dir" ]] || return 0
  printf 'export LD_LIBRARY_PATH=%q${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\n' "$dir" >> "$CONFIG_FILE"
}

maybe_get_optional() {
  # maybe_get_optional VARNAME "Label"
  local key="$1" label="$2" current="${!1:-}"
  if confirm "Enable ${label}?" "${current:+Y}"; then
    ask_path "$key" "Enter ${label} root path" "N" "$current"
  else
    printf -v "$key" '%s' ""
  fi
}
######

##### validators
validate_pinpath() {
  local p="$1"
  local ok=1

  # pin.H can live in .../source/include or .../source/include/pin
  if ! path_has_any "$p" "source/include/pin.H source/include/pin/pin.H"; then
    err "Could not find pin.H under ${p}/source/include[/pin]."
    ok=0
  fi

  # XED include (either xed2 or xed)
  local xed=""
  if   [[ -d "$p/extras/xed2-intel64/include" ]]; then xed="extras/xed2-intel64/include"
  elif [[ -d "$p/extras/xed-intel64/include"  ]]; then xed="extras/xed-intel64/include"
  else
    err "Could not find XED include dir under ${p}/extras/{xed2-intel64,xed-intel64}/include."
    ok=0
  fi

  # basic libs
  if ! [[ -d "$p/intel64/lib" ]]; then
    err "Missing ${p}/intel64/lib"
    ok=0
  fi

  # commit results if everything is OK
  if (( ok )); then
    XED_INC_DIR="$xed"
    return 0
  else
    return 1
  fi
}


validate_hdf5() {
  ask_path HDF5_HOME "Enter HDF5_HOME (prefix that contains include/ and lib/)" "N" "$HDF5_HOME_DEFAULT"
  if [[ -n "${HDF5_HOME:-}" ]]; then
    if ! [[ -f "$HDF5_HOME/include/hdf5.h" ]]; then
      warn "Note: ${HDF5_HOME}/include/hdf5.h not found."
      return 1
    fi
    if ! path_has_any "$HDF5_HOME" "lib/libhdf5.so lib/libhdf5.a"; then
      warn "Note: libhdf5.{so,a} not found under ${HDF5_HOME}/lib."
      return 1
    fi
    return 0  # ok
  fi
  return 1
}
######


bold "zsim environment setup"
info "This will create $(pwd)/${CONFIG_FILE} and ${WRAPPER_FILE}."

# ask for pin
bold "1) Required: Intel Pin"
info "Your SConstruct requires PINPATH (Pin root)."

ask_path PINPATH "Enter PINPATH (Pin root directory)" "Y" "${PINPATH:-}"

# keep asking until all checks pass
while ! validate_pinpath "$PINPATH"; do
  ask_path PINPATH "Enter PINPATH (Pin root directory)" "Y" "${PINPATH}"
done

# hdf5 lib
bold "2) Optional: HDF5"
HDF5_HOME=""
HDF5_HOME_DEFAULT="${HDF5_HOME:-}"
while confirm "Enable HDF5? (sets HDF5_HOME)" "${HDF5_HOME_DEFAULT:+Y}"; do
    if validate_hdf5 "$HDF5_HOME"; then
        break;
    else
        HDF5_HOME=""
        HDF5_HOME_DEFAULT="${HDF5_HOME:-}"
    fi
done

# check memory simulator
bold "3) Optional memory-system simulator libraries"
info "You can enable any of: POLARSSLPATH, DRAMSIMPATH, DRAMSIM3PATH, RAMULATORPATH, RAMULATOR2PATH"

maybe_get_optional DRAMSIMPATH   "DRAMSim (V2) "
maybe_get_optional DRAMSIM3PATH  "DRAMSim (V3)"
maybe_get_optional RAMULATORPATH "Ramulator (V1)"
maybe_get_optional RAMULATOR2PATH "Ramulator (V2)"
maybe_get_optional POLARSSLPATH  "PolarSSL"

# quick non-fatal hints
[[ -n "${POLARSSLPATH:-}"  ]] && [[ -d "$POLARSSLPATH/include" || -d "$POLARSSLPATH/src" ]] || true
[[ -n "${DRAMSIMPATH:-}"   ]] && [[ -d "$DRAMSIMPATH/src"    ]] || true
[[ -n "${DRAMSIM3PATH:-}"  ]] && [[ -d "$DRAMSIM3PATH/inc" || -d "$DRAMSIM3PATH/include" ]] || true
[[ -n "${RAMULATORPATH:-}" ]] && [[ -d "$RAMULATORPATH/src" || -d "$RAMULATORPATH/include" ]] || true
[[ -n "${RAMULATOR2PATH:-}" ]] && [[ -d "$RAMULATOR2PATH/src" ]] || true


# save config 
bold "4) Writing ${CONFIG_FILE}"
: > "$CONFIG_FILE"
echo "# Generated by setup.sh on $(date)" >> "$CONFIG_FILE"
write_kv PINPATH "$PINPATH"
write_kv HDF5_HOME "${HDF5_HOME:-}"
write_kv POLARSSLPATH "${POLARSSLPATH:-}"
write_kv DRAMSIMPATH "${DRAMSIMPATH:-}"
write_kv DRAMSIM3PATH "${DRAMSIM3PATH:-}"
write_kv RAMULATORPATH "${RAMULATORPATH:-}"

path_add_ld "$PINPATH/intel64/lib"
path_add_ld "$PINPATH/intel64/lib-ext"
if [[ -d "$PINPATH/extras/xed2-intel64/lib" ]]; then
  path_add_ld "$PINPATH/extras/xed2-intel64/lib"
elif [[ -d "$PINPATH/extras/xed-intel64/lib" ]]; then
  path_add_ld "$PINPATH/extras/xed-intel64/lib"
fi

[[ -n "${HDF5_HOME:-}" ]] && path_add_ld "$HDF5_HOME/lib"

[[ -n "${POLARSSLPATH:-}"  ]] && { path_add_ld "$POLARSSLPATH/library"; path_add_ld "$POLARSSLPATH/lib"; }
[[ -n "${DRAMSIMPATH:-}"   ]] && { path_add_ld "$DRAMSIMPATH/library";  path_add_ld "$DRAMSIMPATH/lib";  }
[[ -n "${DRAMSIM3PATH:-}"  ]] && { path_add_ld "$DRAMSIM3PATH/lib"; }
[[ -n "${RAMULATORPATH:-}" ]] && { path_add_ld "$RAMULATORPATH/library"; path_add_ld "$RAMULATORPATH/lib"; }

cat >> "$CONFIG_FILE" <<'EOF'

# ---- Convenience ----
# Usage:
#   source .zsim-env
#   scons -j16 --o
#
# Or use the generated ./scons.sh wrapper.
EOF

bold "5) Creating $WRAPPER_FILE"
cat > "$WRAPPER_FILE" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
if [[ -f "${SCRIPT_DIR}/.zsim-env" ]]; then
  # shellcheck disable=SC1091
  source "${SCRIPT_DIR}/.zsim-env"
fi
exec scons "$@"
EOF
chmod +x "$WRAPPER_FILE"

bold "Done!"
info "Saved settings to $(pwd)/${CONFIG_FILE}."
info "Next:"
info "  1) source .zsim-env"
info "  2) scons -j4 --o # or (o/r/d for opt, release and debug)
info "  you can use ./scons.sh to source automatically and build
info ""
info "Re-run ./setup.sh anytime to update paths."
