#!/usr/bin/env bash
set -euo pipefail

resolve_experiment() {
  local input="${1:-}"
  local script_dir repo_root experiment

  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
  if [[ "$script_dir" == */experiments/*/scripts ]]; then
    repo_root="$(cd "$script_dir/../../.." && pwd -P)"
    experiment="$(basename "$(cd "$script_dir/.." && pwd -P)")"
  else
    repo_root="$(cd "$script_dir/.." && pwd -P)"
    if [[ -z "$input" ]]; then
      echo "usage: $0 <experiment-id>" >&2
      exit 1
    fi
    experiment="$input"
  fi

  echo "$repo_root|$experiment"
}

IFS='|' read -r REPO_ROOT EXPERIMENT <<< "$(resolve_experiment "${1:-}")"
MANIFEST="$REPO_ROOT/experiments/$EXPERIMENT/raw-manifest.csv"
TARGET_DIR="$REPO_ROOT/experiments/$EXPERIMENT/raw"

if [[ ! -f "$MANIFEST" ]]; then
  echo "Missing raw manifest: $MANIFEST" >&2
  exit 1
fi

mkdir -p "$TARGET_DIR"
downloaded_any=0

while IFS=$'\t' read -r artifact_role download_url sha256 filename; do
  [[ -z "$download_url" ]] && continue
  output_path="$TARGET_DIR/$filename"
  if ! curl -fL "$download_url" -o "$output_path"; then
    rm -f "$output_path"
    echo "Unavailable remote artifact: $download_url" >&2
    echo "Mirror the file to the VM and update sha256 in $MANIFEST when ready." >&2
    continue
  fi
  echo "Downloaded $output_path"
  if [[ -n "$sha256" && "$sha256" != "PENDING" ]]; then
    echo "$sha256  $output_path" | shasum -a 256 -c -
  fi
  downloaded_any=1
done < <(
  python3 - "$MANIFEST" <<'PY'
import csv
import os
import sys

manifest = sys.argv[1]
with open(manifest, newline="", encoding="utf-8") as fh:
    for row in csv.DictReader(fh):
        url = row.get("download_url", "").strip()
        origin = row.get("origin", "").strip()
        if (
            not url
            or url == "PENDING"
            or origin == "artifact-repo"
            or "<" in url
            or ">" in url
        ):
            continue
        filename = os.path.basename(url.rstrip("/"))
        print(
            "\t".join(
                [
                    row.get("artifact_role", "").strip(),
                    url,
                    row.get("sha256", "").strip(),
                    row.get("relative_path", "").strip() or filename,
                ]
            )
        )
PY
)

if [[ "$downloaded_any" -eq 0 ]]; then
  echo "No downloadable external raw artifacts were found for $EXPERIMENT."
  echo "See $MANIFEST and the Raw Data section in README.md."
fi
