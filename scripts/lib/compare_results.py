#!/usr/bin/env python3
from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


KEY_FIELDS = ("type", "rd_percentage", "pause")

BW_FIELDS = {"bandwidth_bytes_per_second", "bandwidth_ram"}
LAT_FIELDS = {"latency_core_ptr_chase", "latency_mem_ptr_chase", "latency_mem_ptr_chase_ram"}

FIELD_LABELS = {
    "bandwidth_bytes_per_second": "BW (memory interface view)",
    "bandwidth_ram":              "BW (memory simulator view)",
    "latency_core_ptr_chase":     "Latency (application view)",
    "latency_mem_ptr_chase":      "Latency (memory interface view)",
    "latency_mem_ptr_chase_ram":  "Latency (memory simulator view)",
}

COLORS = {
    "lhs": "#1f77b4",
    "rhs": "#d62728",
}


# ── helpers ──────────────────────────────────────────────────────────────────

def resolve_csv(arg: str, repo_root: Path) -> Path:
    candidate = Path(arg)
    if candidate.is_file():
        return candidate.resolve()

    exp_dir = repo_root / "experiments" / arg
    for csv_path in (
        exp_dir / "processed" / "bandwidth_latency.csv",
        exp_dir / "figures" / "bandwidth_latency.csv",
        exp_dir / "results" / "processed" / "bandwidth_latency.csv",
    ):
        if csv_path.is_file():
            return csv_path

    raise FileNotFoundError(f"unable to resolve CSV for '{arg}'")


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as fh:
        return list(csv.DictReader(fh))


def row_key(row: dict[str, str]) -> tuple[str, str, str]:
    return tuple(row.get(field, "") for field in KEY_FIELDS)


def numeric_fields(rows: list[dict[str, str]]) -> list[str]:
    if not rows:
        return []
    fields: list[str] = []
    for field in rows[0].keys():
        if field in KEY_FIELDS:
            continue
        try:
            float(rows[0][field])
        except (TypeError, ValueError):
            continue
        fields.append(field)
    return fields


def _fmt(value: float, field: str) -> str:
    if field in BW_FIELDS:
        return f"{value / 1e9:+.3f} GB/s"
    return f"{value:+.3f} ns"


def _label(field: str) -> str:
    return FIELD_LABELS.get(field, field)


# ── terminal output ───────────────────────────────────────────────────────────

def print_summary(
    lhs_path: Path,
    rhs_path: Path,
    lhs_rows: list[dict[str, str]],
    rhs_rows: list[dict[str, str]],
    shared_keys: list,
    fields: list[str],
    deltas_by_field: dict[str, list[float]],
    lhs_name: str,
    rhs_name: str,
) -> None:
    SEP = "─" * 72

    print()
    print(SEP)
    print("  COMPARISON SUMMARY")
    print(SEP)
    print(f"  A  (lhs)  {lhs_name}")
    print(f"     {lhs_path}")
    print(f"  B  (rhs)  {rhs_name}")
    print(f"     {rhs_path}")
    print(SEP)
    print(f"  {'Rows in A':<22} {len(lhs_rows)}")
    print(f"  {'Rows in B':<22} {len(rhs_rows)}")
    print(f"  {'Shared rows':<22} {len(shared_keys)}")
    lhs_only = len(set(row_key(r) for r in lhs_rows) - set(row_key(r) for r in rhs_rows))
    rhs_only = len(set(row_key(r) for r in rhs_rows) - set(row_key(r) for r in lhs_rows))
    if lhs_only:
        print(f"  {'A-only rows':<22} {lhs_only}  ← rows missing from B")
    if rhs_only:
        print(f"  {'B-only rows':<22} {rhs_only}  ← rows missing from A")
    print(SEP)

    if not fields:
        print("  (no shared numeric fields)")
        print(SEP)
        return

    col_w = [32, 13, 13, 13]
    header = (
        f"  {'Metric':<{col_w[0]}}"
        f"{'Mean Δ (A−B)':>{col_w[1]}}"
        f"{'Mean |Δ|':>{col_w[2]}}"
        f"{'Max |Δ|':>{col_w[3]}}"
    )
    print(header)
    print("  " + "·" * (sum(col_w)))

    for field in fields:
        deltas = deltas_by_field.get(field, [])
        if not deltas:
            continue
        max_abs = max(abs(v) for v in deltas)
        mean_abs = sum(abs(v) for v in deltas) / len(deltas)
        signed_mean = sum(deltas) / len(deltas)
        if math.isnan(max_abs):
            continue
        label = _label(field)
        print(
            f"  {label:<{col_w[0]}}"
            f"{_fmt(signed_mean, field):>{col_w[1]}}"
            f"{_fmt(mean_abs, field):>{col_w[2]}}"
            f"{_fmt(max_abs, field):>{col_w[3]}}"
        )

    print(SEP)
    print()


# ── plotting ─────────────────────────────────────────────────────────────────

def _curves_by_rd(
    rows: list[dict[str, str]],
    bw_field: str,
    lat_field: str,
) -> dict[int, tuple[list[float], list[float]]]:
    """Group rows by rd_percentage, sort each group by pause, return (bw, lat) pairs."""
    from collections import defaultdict
    buckets: dict[int, list[tuple[float, float, float]]] = defaultdict(list)
    for row in rows:
        try:
            rd = int(float(row["rd_percentage"]))
            pause = float(row["pause"])
            bw = float(row[bw_field]) / 1e9
            lat = float(row[lat_field])
            buckets[rd].append((pause, bw, lat))
        except (KeyError, ValueError):
            continue
    result: dict[int, tuple[list[float], list[float]]] = {}
    for rd, points in sorted(buckets.items()):
        points.sort(key=lambda t: t[0])
        result[rd] = ([p[1] for p in points], [p[2] for p in points])
    return result


def _rd_color(rd_value: int, cmap_lhs, cmap_rhs):
    """Replicate plot.py's calculate_color() for both cmaps."""
    mn, mx = 0.2, 1.0
    factor = (100.0 - 0.0) / (mx - mn)
    rw_reverse = 75.0 + 75.0 - rd_value
    c = (rw_reverse - 50.0) / factor + mn
    c = max(mn, min(mx, c))
    return cmap_lhs(c), cmap_rhs(c)


def make_plots(
    lhs_rows: list[dict[str, str]],
    rhs_rows: list[dict[str, str]],
    lhs_name: str,
    rhs_name: str,
    output_path: Path,
) -> None:
    plot_specs = [
        ("bandwidth_ram",              "latency_mem_ptr_chase_ram", "Memory simulator view"),
        ("bandwidth_bytes_per_second", "latency_mem_ptr_chase",    "Memory interface view"),
        ("bandwidth_bytes_per_second", "latency_core_ptr_chase",   "Application view"),
    ]

    # only keep specs where both fields exist in at least one dataset
    available_specs = []
    for bw_f, lat_f, title in plot_specs:
        def _has(rows: list[dict[str, str]], bf: str, lf: str) -> bool:
            for r in rows:
                try:
                    float(r[bf]); float(r[lf])
                    return True
                except (KeyError, ValueError):
                    continue
            return False
        if _has(lhs_rows, bw_f, lat_f) or _has(rhs_rows, bw_f, lat_f):
            available_specs.append((bw_f, lat_f, title))

    if not available_specs:
        return

    try:
        cmap_lhs = matplotlib.colormaps.get_cmap("Blues")
        cmap_rhs = matplotlib.colormaps.get_cmap("Reds")
    except AttributeError:
        cmap_lhs = matplotlib.cm.get_cmap("Blues")
        cmap_rhs = matplotlib.cm.get_cmap("Reds")

    MAX_BW_GB = 128.0
    YMAX = 400

    n = len(available_specs)
    fig, axes = plt.subplots(1, n, figsize=(7 * n, 5), squeeze=False,
                             sharey=True)

    for ax, (bw_f, lat_f, title) in zip(axes[0], available_specs):
        lhs_curves = _curves_by_rd(lhs_rows, bw_f, lat_f)
        rhs_curves = _curves_by_rd(rhs_rows, bw_f, lat_f)
        all_rds = sorted(set(lhs_curves) | set(rhs_curves))

        plotted_lhs = plotted_rhs = False
        for rd in all_rds:
            c_lhs, c_rhs = _rd_color(rd, cmap_lhs, cmap_rhs)
            if rd in lhs_curves:
                bws, lats = lhs_curves[rd]
                ax.plot(bws, lats, color=c_lhs, linewidth=1.2, alpha=0.85,
                        label=lhs_name if not plotted_lhs else "_nolegend_")
                plotted_lhs = True
            if rd in rhs_curves:
                bws, lats = rhs_curves[rd]
                ax.plot(bws, lats, color=c_rhs, linewidth=1.2, alpha=0.85,
                        linestyle="--",
                        label=rhs_name if not plotted_rhs else "_nolegend_")
                plotted_rhs = True

        mn, mx = 0.2, 1.0
        factor = (100.0 - 0.0) / (mx - mn)
        rw_reverse = 75.0 + 75.0 - 75.0
        c_vline = max(mn, min(mx, (rw_reverse - 50.0) / factor + mn))
        ax.axvline(x=MAX_BW_GB, color=cmap_lhs(c_vline), linewidth=4,
                   linestyle=":", label=f"Max. theoretical BW = {int(MAX_BW_GB)} GB/s",
                   zorder=5)
        ax.set_xlim(0, MAX_BW_GB * 1.05)
        ax.set_ylim(0, YMAX)
        ax.set_title(title, fontsize=11, fontweight="bold")
        ax.set_xlabel("Used Memory bandwidth [GB/s]", fontsize=9)
        ax.set_ylabel("Memory access latency [ns]", fontsize=9)
        ax.legend(fontsize=8)
        ax.grid(True, linestyle="--", alpha=0.35)
        ax.tick_params(labelsize=8)

    fig.suptitle(
        f"Bandwidth\u2013Latency comparison\n{lhs_name}  vs  {rhs_name}",
        fontsize=12, fontweight="bold", y=1.02,
    )
    fig.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"  Plot saved → {output_path}")
    print()


# ── main ──────────────────────────────────────────────────────────────────────

def main() -> int:
    if len(sys.argv) < 3:
        print("usage: compare-results.sh <lhs-experiment-or-csv> <rhs-experiment-or-csv>", file=sys.stderr)
        return 1

    repo_root = Path(__file__).resolve().parent.parent.parent
    lhs_arg, rhs_arg = sys.argv[1], sys.argv[2]
    lhs_path = resolve_csv(lhs_arg, repo_root)
    rhs_path = resolve_csv(rhs_arg, repo_root)

    lhs_name = lhs_arg if not Path(lhs_arg).is_file() else Path(lhs_arg).parts[-3]
    rhs_name = rhs_arg if not Path(rhs_arg).is_file() else Path(rhs_arg).parts[-3]

    lhs_rows = load_rows(lhs_path)
    rhs_rows = load_rows(rhs_path)
    lhs_map = {row_key(row): row for row in lhs_rows}
    rhs_map = {row_key(row): row for row in rhs_rows}
    shared_keys = sorted(set(lhs_map) & set(rhs_map))

    fields = [f for f in numeric_fields(lhs_rows) if f in numeric_fields(rhs_rows)]
    deltas_by_field: dict[str, list[float]] = {}
    for field in fields:
        deltas: list[float] = []
        for key in shared_keys:
            try:
                deltas.append(float(lhs_map[key][field]) - float(rhs_map[key][field]))
            except (KeyError, ValueError):
                continue
        deltas_by_field[field] = deltas

    print_summary(
        lhs_path, rhs_path,
        lhs_rows, rhs_rows,
        shared_keys, fields, deltas_by_field,
        lhs_name, rhs_name,
    )

    plot_out = repo_root / "test-output" / "compare" / f"{lhs_name}_vs_{rhs_name}.png"
    make_plots(lhs_rows, rhs_rows, lhs_name, rhs_name, plot_out)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
