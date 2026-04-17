#!/usr/bin/env python3
"""
Bandwidth/latency post-processing for ZSim+Ramulator measurements.
Modified to match specific plotter styles (fonts, sizes, colors) and output.pdf.
"""

from __future__ import annotations
import matplotlib.pyplot as plt

import argparse
import math
import os
import re
import sys
from typing import Dict, List, Tuple

import pandas as pd

import matplotlib

matplotlib.use("Agg")


STATIC_CONFIG: Dict[str, str] = {
    "NAME": "ZSim+Ramulator",

    "CPU": "OOO (32 cores)",
    "CPU_FREQ": "2.1",                         # CPU_FREQ (GHZ)

    "MEM_TYPE": "DDR4",
    "MEM_FREQ": "1.3333333",                        # MEM_FREQ (GHZ)
    "MEM_MAX_CHANNELS": "6",                  # total channel of the MEM config
    "MEM_CHANNELS_PER_MEM_INSTANCE": "1",     # number of mem instance in zsim
    # zsim phase length (compute total sim cycle)
    "PHASE_LENGTH": "1000",
    "LINE_SIZE": "64",
    "PTR_CHASE_CORE_ID": "0",

    "STATS_PATH": "zsim.out",  # zsim stat output filename
    "RAMULATOR_STATS_PATH": "stream_mem-0.ramulator.stats",
    "PLOT_MAX_BW_LABEL_Y": "370",
    "PLOT_LATENCY_YMAX": "400",
    "PLOT_DPI": "0",
}


# ======================= CFG helpers ========================
def cfg_int(config: Dict[str, str], key: str) -> int:
    if key not in config:
        raise KeyError(f"Missing required config key '{key}'")
    value = str(config[key]).strip().strip('"')
    try:
        return int(value)
    except ValueError as err:
        raise ValueError(f"Invalid integer for key '{key}': {value}") from err


def cfg_float(config: Dict[str, str], key: str) -> float:
    if key not in config:
        raise KeyError(f"Missing required config key '{key}'")
    value = str(config[key]).strip().strip('"')
    try:
        return float(value)
    except ValueError as err:
        raise ValueError(f"Invalid float for key '{key}': {value}") from err


def cfg_str(config: Dict[str, str], key: str) -> str:
    if key not in config:
        raise KeyError(f"Missing required config key '{key}'")
    value = config[key]
    return str(value).strip().strip('"')
# ==========================================================


def normalize_stats_path(stats_path: str) -> Tuple[str, str]:

    cleaned = str(stats_path or "").strip().strip('"')
    if not cleaned:
        raise ValueError("STATS_PATH is empty")
    cleaned = cleaned.strip("/")
    stats_dir, stats_leaf = os.path.split(cleaned)
    if not stats_leaf:
        raise ValueError(f"STATS_PATH '{stats_path}' is invalid")
    if not stats_leaf.endswith(".zsim.out"):
        # raise ValueError(f"STATS_PATH '{stats_path}' must include .zsim.out filename")
        # print("oopss")
        pass
    if stats_dir in {"", "."}:
        stats_dir = ""
    return stats_dir, stats_leaf


# =============== zsim.out patterns of interest =================
CORE_RE = re.compile(r"^\s*([A-Za-z0-9_.-]+)-(\d+):\s+# Core stats\s*$")
MEMCTRL_RE = re.compile(r"^\s*mem-(\d+):\s+# Memory controller stats\s*$")
RAMULATOR_TRACK_RE = re.compile(
    r"^\s*ramulator\.track_cores\.core(\d+)\.read_latency_avg\s+([0-9Ee.+-]+)")
RAMULATOR_TRACK_RE = re.compile(
    r"^\s*ramulator\.track_cores\.core(\d+)\.read_latency_avg\s+([0-9Ee.+-]+)")
# ==========================================================


def detect_topology_from_zsim(trace_path: str) -> Dict[str, object]:
    core_ids: set[int] = set()
    ctrl_ids: set[int] = set()

    if not trace_path or not os.path.isfile(trace_path):
        return {"core_count": 0, "controller_count": 0, "channels_per_memory": []}

    with open(trace_path, "r", encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            match_core = CORE_RE.match(line)
            if match_core:
                core_ids.add(int(match_core.group(2)))
                continue
            match_ctrl = MEMCTRL_RE.match(line)
            if match_ctrl:
                ctrl_ids.add(int(match_ctrl.group(1)))

    core_count = (max(core_ids) + 1) if core_ids else 0
    controller_count = (max(ctrl_ids) + 1) if ctrl_ids else 0
    return {
        "core_count": core_count,
        "controller_count": controller_count,
        "channels_per_memory": [],
    }


def augment_with_channel_info(topology: Dict[str, object], channels_per_controller: int) -> Dict[str, object]:
    topo = dict(topology)
    controller_count = int(topo.get("controller_count", 0))
    cpc = max(1, channels_per_controller)
    topo["channels_per_controller"] = cpc
    topo["channels_per_memory"] = [cpc for _ in range(
        controller_count)] if controller_count > 0 else []
    return topo


def init_bandwidth_state(config: Dict[str, str], topology: Dict[str, object]) -> Dict[str, object]:
    detected_ctls = int(topology.get("controller_count", 0)) if topology else 0
    controller_count = detected_ctls if detected_ctls > 0 else max(
        1, cfg_int(config, "MEM_MAX_CHANNELS"))

    return {
        "controller_count": controller_count,
        "rd_counter": 0,
        "wr_counter": 0,
    }


def parse_bandwidth_line(line: str, state: Dict[str, object]) -> Tuple[str | None, float | None, bool]:
    sanitized = line.replace("#", ":").replace(" ", "")
    tokens = sanitized.split(":")
    if not tokens:
        return None, None, False

    label = tokens[0]
    if label == "Runtime(RDTSC)[s]":
        try:
            value = float(tokens[2])
        except (IndexError, ValueError):
            value = float("nan")
        return "runtime", value, False
    if label == "STAT":
        return None, None, False
    if label == "rd":
        controller_count = int(state["controller_count"])
        state["rd_counter"] = (int(state["rd_counter"]) + 1) % controller_count
        stat = f"rd_access_socket_MC_{state['rd_counter']}"
        try:
            value = float(tokens[1])
        except (IndexError, ValueError):
            value = float("nan")
        return stat, value, False
    if label == "wr":
        controller_count = int(state["controller_count"])
        state["wr_counter"] = (int(state["wr_counter"]) + 1) % controller_count
        stat = f"wr_access_socket_MC_{state['wr_counter']}"
        try:
            value = float(tokens[1])
        except (IndexError, ValueError):
            value = float("nan")
        return stat, value, False
    if label == "cycles":
        return None, None, False
    if label == "heartbeats":
        return None, None, True
    return None, None, False


def list_measurement_dirs(root: str, stats_dir: str, stats_filename: str) -> List[str]:
    entries: List[str] = []
    for name in sorted(os.listdir(root)):
        candidate = os.path.join(root, name)
        if not os.path.isdir(candidate):
            continue
        targets = []
        if stats_dir:
            targets.append(os.path.join(candidate, stats_dir, stats_filename))
        targets.append(os.path.join(candidate, stats_filename))
        if any(os.path.isfile(path) for path in targets):
            entries.append(name)
    return entries


def resolve_trace_path(root: str, measurement: str, stats_dir: str, stats_filename: str) -> str:
    base = os.path.join(root, measurement)
    nested = os.path.join(
        base, stats_dir, stats_filename) if stats_dir else None
    direct = os.path.join(base, stats_filename)
    if nested and os.path.isfile(nested):
        return nested
    if os.path.isfile(direct):
        return direct
    return nested or direct


def parse_bandwidth_measurements(
    measurement_root: str,
    measurement_dirs: List[str],
    config: Dict[str, str],
    stats_dir: str,
    stats_filename: str,
    topology: Dict[str, object],
) -> pd.DataFrame:
    type_label = f"{cfg_str(config, 'MEM_TYPE')}-{cfg_str(config, 'MEM_FREQ')}"
    records: List[Dict[str, object]] = []

    for name in measurement_dirs:
        tokens = name.rstrip(".txt").split("_")
        rd_value = int(tokens[1]) if len(
            tokens) > 1 and tokens[1].isdigit() else 0
        pause_value = int(tokens[2]) if len(
            tokens) > 2 and tokens[2].isdigit() else 0
        trace_path = resolve_trace_path(
            measurement_root, name, stats_dir, stats_filename)
        if not trace_path or not os.path.isfile(trace_path):
            continue

        state = init_bandwidth_state(config, topology)
        base_record = {
            "type": type_label,
            "rd_percentage": rd_value,
            "pause": pause_value,
            "repeat": 0,
            "measure": 1,
        }

        with open(trace_path, "r", encoding="utf-8", errors="ignore") as fh:
            current_record = dict(base_record)
            measure_idx = 1
            phase_count: int | None = None

            for line in fh:
                stripped = line.strip()
                if stripped.startswith("phase:"):
                    try:
                        phase_token = stripped.split(
                            ":", 1)[1].split("#", 1)[0].strip()
                        phase_count = int(phase_token)
                    except (IndexError, ValueError):
                        phase_count = None
                stat, value, done = parse_bandwidth_line(line, state)
                if stat is not None and value is not None:
                    if stat == "runtime":
                        continue
                    current_record[stat] = int(value)
                if done:
                    if len(current_record) > len(base_record):
                        if phase_count is not None:
                            current_record["phase"] = phase_count
                        records.append(current_record.copy())
                        measure_idx += 1
                    current_record = dict(base_record)
                    current_record["measure"] = measure_idx

    if not records:
        return pd.DataFrame()

    df = pd.DataFrame.from_records(records)
    df.sort_values(by=["type", "rd_percentage",
                   "pause", "measure"], inplace=True)
    df.reset_index(drop=True, inplace=True)
    return df


def parse_core_latency_measurements(
    measurement_root: str,
    measurement_dirs: List[str],
    config: Dict[str, str],
    stats_dir: str,
    stats_filename: str,
) -> pd.DataFrame:
    type_label = f"{cfg_str(config, 'MEM_TYPE')}-{cfg_str(config, 'MEM_FREQ')}"
    ptr_core_id = cfg_int(config, "PTR_CHASE_CORE_ID")
    records: List[Dict[str, object]] = []

    for name in measurement_dirs:
        tokens = name.rstrip(".txt").split("_")
        rd_value = int(tokens[1]) if len(
            tokens) > 1 and tokens[1].isdigit() else 0
        pause_value = int(tokens[2]) if len(
            tokens) > 2 and tokens[2].isdigit() else 0
        trace_path = resolve_trace_path(
            measurement_root, name, stats_dir, stats_filename)
        if not trace_path or not os.path.isfile(trace_path):
            continue

        record = parse_latency_measurement(
            trace_path,
            {
                "type": type_label,
                "rd_percentage": rd_value,
                "pause": pause_value,
                "repeat": 0,
                "measure": 1,
            },
            ptr_core_id,
        )
        if record:
            records.append(record)

    if not records:
        return pd.DataFrame()

    df = pd.DataFrame.from_records(records)
    df.sort_values(by=["type", "rd_percentage", "pause"], inplace=True)
    df.reset_index(drop=True, inplace=True)
    return df


def parse_latency_measurement(trace_path: str, base_record: Dict[str, object], ptr_core_id: int) -> Dict[str, object] | None:
    cycles: int | None = None
    instrs: int | None = None
    in_target = False

    with open(trace_path, "r", encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            match_core = CORE_RE.match(line)
            if match_core:
                in_target = int(match_core.group(2)) == ptr_core_id
                continue

            if not in_target:
                continue

            stripped = line.strip()
            if stripped.startswith("cycles:"):
                try:
                    cycles = int(stripped.split(":", 1)[
                                 1].split("#", 1)[0].strip())
                except (IndexError, ValueError):
                    cycles = None
            elif stripped.startswith("instrs:"):
                try:
                    instrs = int(stripped.split(":", 1)[
                                 1].split("#", 1)[0].strip())
                except (IndexError, ValueError):
                    instrs = None

    if cycles is None or instrs is None:
        return None

    record = dict(base_record)
    record[f"core-{ptr_core_id}_cycles"] = cycles
    record[f"core-{ptr_core_id}_instrs"] = instrs
    return record


def extract_core_tracker_latency(trace_path: str, ptr_core_id: int) -> int | None:
    tracker_active = False
    in_core = False
    core_header = f"core{ptr_core_id}:"
    latency_key = f"core{ptr_core_id}.avg_read_latency:"

    with open(trace_path, "r", encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            stripped = line.strip()
            if stripped.startswith("trackCores:"):
                tracker_active = True
                in_core = False
                continue
            if not tracker_active:
                continue

            if not line.startswith(" ") and stripped:
                break

            if stripped.startswith("core") and stripped.endswith("# Tracked core stats"):
                in_core = stripped.startswith(core_header)
                continue

            if in_core and stripped.startswith(latency_key):
                try:
                    return int(stripped.split(":", 1)[1].split("#", 1)[0].strip())
                except (IndexError, ValueError):
                    return None

    return None


def extract_ramulator_core_latency(ram_path: str, ptr_core_id: int) -> float | None:
    if not os.path.isfile(ram_path):
        return None
    with open(ram_path, "r", encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            match = RAMULATOR_TRACK_RE.match(line)
            if match and int(match.group(1)) == ptr_core_id:
                try:
                    return float(match.group(2))
                except ValueError:
                    return None
    return None

def extract_ramulator_mem_bandwidth(ram_path: str) -> float | None:
    """
    Extract the total (read + write) memory bandwidth in Bps from Ramulator stats file.
    Returns None if file not found or values not present.
    """
    if not os.path.isfile(ram_path):
        return None

    read_bw = None
    write_bw = None

    # Regex to match the global bandwidth lines (flexible with leading spaces)
    read_pattern = re.compile(r'^\s*ramulator\.read_bandwidth\s+([\d\.]+)')
    write_pattern = re.compile(r'^\s*ramulator\.write_bandwidth\s+([\d\.]+)')

    with open(ram_path, "r", encoding="utf-8", errors="ignore") as fh:
        for line in fh:
            if read_bw is None:
                m = read_pattern.match(line)
                if m:
                    read_bw = float(m.group(1))
            if write_bw is None:
                m = write_pattern.match(line)
                if m:
                    write_bw = float(m.group(1))
            if read_bw is not None and write_bw is not None:
                break  # Early exit once both found

    if read_bw is None or write_bw is None:
        return None

    return 6 * (read_bw + write_bw)  # Total bandwidth in Bps


def parse_ptrchase_mem_latency(
    measurement_root: str,
    measurement_dirs: List[str],
    config: Dict[str, str],
    stats_dir: str,
    stats_filename: str,
) -> pd.DataFrame:
    records: List[Dict[str, object]] = []
    type_label = f"{cfg_str(config, 'MEM_TYPE')}-{cfg_str(config, 'MEM_FREQ')}"
    cpu_freq = cfg_float(config, "CPU_FREQ")
    if cpu_freq <= 0:
        raise ValueError("CPU_FREQ must be positive")
    ptr_core_id = cfg_int(config, "PTR_CHASE_CORE_ID")

    for name in measurement_dirs:
        tokens = name.rstrip(".txt").split("_")
        rd_value = int(tokens[1]) if len(
            tokens) > 1 and tokens[1].isdigit() else 0
        pause_value = int(tokens[2]) if len(
            tokens) > 2 and tokens[2].isdigit() else 0
        trace_path = resolve_trace_path(
            measurement_root, name, stats_dir, stats_filename)
        if not trace_path or not os.path.isfile(trace_path):
            continue

        tracker_latency = extract_core_tracker_latency(trace_path, ptr_core_id)
        if tracker_latency is None:
            continue
        latency = tracker_latency / cpu_freq

        records.append(
            {
                "type": type_label,
                "rd_percentage": rd_value,
                "pause": pause_value,
                "latency_mem_ptr_chase": latency,
            }
        )

    if not records:
        return pd.DataFrame()

    df = pd.DataFrame.from_records(records)
    df.sort_values(by=["type", "rd_percentage", "pause"], inplace=True)
    df.reset_index(drop=True, inplace=True)
    return df


def parse_ramulator_mem_latency(
    measurement_root: str,
    measurement_dirs: List[str],
    config: Dict[str, str],
) -> pd.DataFrame:
    records: List[Dict[str, object]] = []
    type_label = f"{cfg_str(config, 'MEM_TYPE')}-{cfg_str(config, 'MEM_FREQ')}"
    ram_freq = cfg_float(config, "MEM_FREQ")
    if ram_freq <= 0:
        raise ValueError("CPU_FREQ must be positive")
    ptr_core_id = cfg_int(config, "PTR_CHASE_CORE_ID")
    ram_file = cfg_str(config, "RAMULATOR_STATS_PATH")

    for name in measurement_dirs:
        tokens = name.rstrip(".txt").split("_")
        rd_value = int(tokens[1]) if len(
            tokens) > 1 and tokens[1].isdigit() else 0
        pause_value = int(tokens[2]) if len(
            tokens) > 2 and tokens[2].isdigit() else 0
        ram_path = os.path.join(measurement_root, name, ram_file)
        latency_cycles = extract_ramulator_core_latency(ram_path, ptr_core_id)
        if latency_cycles is None:
            continue
        latency_ns = latency_cycles / ram_freq
        records.append(
            {
                "type": type_label,
                "rd_percentage": rd_value,
                "pause": pause_value,
                "latency_mem_ptr_chase_ram": latency_ns,
            }
        )

    if not records:
        return pd.DataFrame()

    df = pd.DataFrame.from_records(records)
    df.sort_values(by=["type", "rd_percentage", "pause"], inplace=True)
    df.reset_index(drop=True, inplace=True)
    return df

def parse_ramulator_mem_bandwidth(
    measurement_root: str,
    measurement_dirs: List[str],
    config: Dict[str, str],
) -> pd.DataFrame:
    records: List[Dict[str, object]] = []
    type_label = f"{cfg_str(config, 'MEM_TYPE')}-{cfg_str(config, 'MEM_FREQ')}"
    ram_freq = cfg_float(config, "MEM_FREQ")
    if ram_freq <= 0:
        raise ValueError("CPU_FREQ must be positive")
    ptr_core_id = cfg_int(config, "PTR_CHASE_CORE_ID")
    ram_file = cfg_str(config, "RAMULATOR_STATS_PATH")

    for name in measurement_dirs:
        tokens = name.rstrip(".txt").split("_")
        rd_value = int(tokens[1]) if len(
            tokens) > 1 and tokens[1].isdigit() else 0
        pause_value = int(tokens[2]) if len(
            tokens) > 2 and tokens[2].isdigit() else 0
        ram_path = os.path.join(measurement_root, name, ram_file)
        mem_bandwidth = extract_ramulator_mem_bandwidth(ram_path)
        if mem_bandwidth is None:
            continue
        mem_bandwidth = mem_bandwidth # in bytes per second (TODO: check units)
        records.append(
            {
                "type": type_label,
                "rd_percentage": rd_value,
                "pause": pause_value,
                "bandwidth_ram": mem_bandwidth,
            }
        )

    if not records:
        return pd.DataFrame()

    df = pd.DataFrame.from_records(records)
    df.sort_values(by=["type", "rd_percentage", "pause"], inplace=True)
    df.reset_index(drop=True, inplace=True)
    return df


def calculate_bandwidth(df: pd.DataFrame, config: Dict[str, str]) -> pd.DataFrame:
    cas_columns = [c for c in df.columns if "access_socket" in c]
    if not cas_columns or "phase" not in df.columns:
        return pd.DataFrame()

    line_size = cfg_int(config, "LINE_SIZE")
    phase_length = cfg_int(config, "PHASE_LENGTH")
    cpu_freq_cycles_per_ns = cfg_float(config, "CPU_FREQ")
    if phase_length <= 0 or cpu_freq_cycles_per_ns <= 0:
        raise ValueError("PHASE_LENGTH and CPU_FREQ must be positive")

    access_sum = df[cas_columns].sum(axis=1)
    phase_counts = df["phase"].replace(0, math.nan)
    runtime_cycles = phase_counts * phase_length
    bytes_per_cycle = access_sum * line_size / runtime_cycles
    bytes_per_second = bytes_per_cycle * cpu_freq_cycles_per_ns * 1e9

    df = df.copy()
    df["bandwidth_bytes_per_second"] = bytes_per_second
    drop_cols = list({*cas_columns, "phase"})
    extra_cycles = [c for c in df.columns if c.startswith("cycles_")]
    drop_cols.extend(extra_cycles)
    df.drop(columns=drop_cols, inplace=True, errors="ignore")
    return df


def calculate_latency(df: pd.DataFrame, config: Dict[str, str]) -> pd.DataFrame:
    cpu_freq = cfg_float(config, "CPU_FREQ")
    if cpu_freq <= 0:
        raise ValueError("CPU_FREQ must be positive")

    ptr_core_id = cfg_int(config, "PTR_CHASE_CORE_ID")
    cycles_col = f"core-{ptr_core_id}_cycles"
    instrs_col = f"core-{ptr_core_id}_instrs"
    if cycles_col not in df.columns or instrs_col not in df.columns:
        return pd.DataFrame()

    df = df.copy()
    df["latency_core_ptr_chase"] = float("nan")
    valid_instrs = df[instrs_col] != 0
    df.loc[valid_instrs, "latency_core_ptr_chase"] = (
        df.loc[valid_instrs, cycles_col] / df.loc[valid_instrs, instrs_col]) / cpu_freq
    df.drop(columns=[col for col in df.columns if col.startswith(
        "MC_")], inplace=True, errors="ignore")
    df.drop(columns=[cycles_col, instrs_col], inplace=True, errors="ignore")
    return df


def aggregate_metric(df: pd.DataFrame, value_col: str) -> pd.DataFrame:
    if df.empty or value_col not in df.columns:
        return pd.DataFrame()

    group_cols = ["type", "rd_percentage", "pause"]
    grouped = df.groupby(group_cols, as_index=False)[value_col].mean()
    return grouped


def split_by_rd_percentage(
    df: pd.DataFrame,
    latency_col: str,
    bandwidth_col: str = "bandwidth_bytes_per_second"  # default for backward compat
) -> Dict[int, pd.DataFrame]:
    mapping: Dict[int, pd.DataFrame] = {}
    if latency_col not in df.columns or bandwidth_col not in df.columns:
        return mapping

    for rd_value in sorted(df["rd_percentage"].unique()):
        subset = df[df["rd_percentage"] == rd_value].copy()
        # subset.sort_values(by=bandwidth_col, inplace=True)
        subset.sort_values(by="pause", inplace=True)
        subset.reset_index(drop=True, inplace=True)

        # GB/s for plotting
        subset["bandwidth_smooth"] = subset[bandwidth_col] / 1e9
        subset["latency_smooth"] = subset[latency_col]

        mapping[int(rd_value)] = subset

    return mapping


def calculate_color(rd_value: int) -> Tuple[float, float, float, float]:
    """
    Calculates color based on read percentage to match plotter style.
    Plotter logic (inverted here for rd%):
      Low read % (Write heavy) -> Darker Blue
      High read % (Read heavy) -> Lighter Blue
    """
    try:
        cmap = matplotlib.colormaps.get_cmap("Blues") # type: ignore[attr-defined]
    except AttributeError:
        cmap = matplotlib.cm.get_cmap("Blues")
    
    # normalized = 1.0 - max(0.0, min(1.0, rd_value / 100.0))
    
    # min_c, max_c = 0.2, 1.0
    # normalized = normalized * (max_c - min_c) + min_c
    # return cmap(normalized)

    min = 0.2
    max = 1
    factor = (100.0 - 0.0)/(max-min) #convert 50-100 interval to min-max
    rw_reverse = 75.0 + 75.0 - rd_value
    c = (rw_reverse - 50.0)/factor + min
    #c = (rw - 50.0)/factor + min
    return cmap(c)
    #return (c, c, c, 0.9)
    
    



def generate_plot(
    dfs_rw: Dict[int, pd.DataFrame],
    config: Dict[str, str],
    topology: Dict[str, object] | None,
    output_path: str,
    title: str,
) -> None:
    if not dfs_rw:
        raise RuntimeError("No data available to plot.")

    plt.rcParams['font.size'] = 38
    
    total_channels = None
    if topology:
        channels = topology.get("channels_per_memory", [])
        if isinstance(channels, list) and channels:
            total_channels = sum(int(ch) for ch in channels)
    if not total_channels:
        total_channels = cfg_int(config, "MEM_MAX_CHANNELS")

    mem_freq = cfg_float(config, "MEM_FREQ")
    print(f"the total channels are : {total_channels}")
    max_bw = total_channels * 8 * (2 * mem_freq)

    fig, ax = plt.subplots(figsize=(16, 9))
    # ax.grid(True, linestyle="--", linewidth=0.4, alpha=0.5)

    max_latency = 0.0
    for rd_value, df in dfs_rw.items():
        color = calculate_color(rd_value)
        ax.plot(
            df["bandwidth_smooth"],
            df["latency_smooth"],
            color=color,
            linewidth=1.0,
            marker=None,
            label=f"rd={rd_value}%",
        )
        max_latency = max(max_latency, df["latency_smooth"].max())

    ax.axvline(x=max_bw, color=calculate_color(75), linewidth=4, linestyle=':', label='max-bandwidth')
    
    ax.text(max_bw, cfg_float(config, "PLOT_MAX_BW_LABEL_Y"),
            f'Max. theoretical BW = {round(max_bw)} GB/s', 
            color='black', horizontalalignment='right', fontsize=38)

    ax.set_xlim(0, max_bw * 1.05)
    ax.set_ylim(0, cfg_float(config, "PLOT_LATENCY_YMAX"))
    
    ax.set_xlabel("Used Memory bandwidth [GB/s]", fontsize=38)
    ax.set_ylabel("Memory access latency [ns]", fontsize=38)
    ax.set_title(title)
    
    # Tick adjustments
    ax.tick_params(axis='x', labelsize=38)
    ax.tick_params(axis='y', labelsize=38)
    
    # Removed legend
    # ax.legend(loc="best")

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    fig.tight_layout()
    plot_dpi = cfg_int(config, "PLOT_DPI")
    save_kwargs = {"dpi": plot_dpi} if plot_dpi > 0 else {}
    fig.savefig(output_path, **save_kwargs)
    png_dir = os.path.join(os.path.dirname(output_path), "pngs")
    png_name = os.path.splitext(os.path.basename(output_path))[0] + ".png"
    os.makedirs(png_dir, exist_ok=True)
    fig.savefig(os.path.join(png_dir, png_name), **save_kwargs)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Process bandwidth/latency measurements.")
    parser.add_argument(
        "working_dir", help="Path containing measurement subdirectories")
    parser.add_argument(
        "--output-dir",
        help="Directory that will receive processed/ and figures/. Defaults to <repo>/test-output/<stage-or-config-name>.",
    )
    parser.add_argument(
        "--config-dir",
        help="Directory used to derive the default output folder name. Defaults to working_dir.",
    )
    args = parser.parse_args()

    working_dir = os.path.abspath(args.working_dir)
    if not os.path.isdir(working_dir):
        print(f"Error: {working_dir} is not a directory.", file=sys.stderr)
        return 1
    config_dir = os.path.abspath(args.config_dir) if args.config_dir else working_dir
    if not os.path.isdir(config_dir):
        print(f"Error: {config_dir} is not a directory.", file=sys.stderr)
        return 1
    if args.output_dir:
        output_dir = os.path.abspath(args.output_dir)
    else:
        repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        default_name = os.path.basename(os.path.normpath(config_dir or working_dir))
        if not default_name:
            default_name = os.path.basename(os.path.normpath(working_dir)) or "plot-output"
        output_dir = os.path.join(repo_root, "test-output", default_name)

    config = STATIC_CONFIG.copy()

    try:
        stats_dir, stats_filename = normalize_stats_path(
            cfg_str(config, "STATS_PATH"))
    except ValueError as err:
        print(f"Error: {err}", file=sys.stderr)
        return 1
    measurement_dirs = list_measurement_dirs(
        working_dir, stats_dir, stats_filename)
    if not measurement_dirs:
        print("Error: no measurement directories with stats files found.",
              file=sys.stderr)
        return 1

    representative_trace = resolve_trace_path(
        working_dir, measurement_dirs[0], stats_dir, stats_filename)
    topo_raw = detect_topology_from_zsim(representative_trace)
    topology = augment_with_channel_info(
        topo_raw, cfg_int(config, "MEM_CHANNELS_PER_MEM_INSTANCE"))

    bandwidth_raw = parse_bandwidth_measurements(
        working_dir, measurement_dirs, config, stats_dir, stats_filename, topology)
    if bandwidth_raw.empty:
        print("Error: unable to parse bandwidth data.", file=sys.stderr)
        return 1
    bandwidth = calculate_bandwidth(bandwidth_raw, config)
    if bandwidth.empty:
        print("Error: bandwidth dataframe is empty after processing.", file=sys.stderr)
        return 1

    latency_raw = parse_core_latency_measurements(
        working_dir, measurement_dirs, config, stats_dir, stats_filename)
    if latency_raw.empty:
        print("Error: unable to parse latency data.", file=sys.stderr)
        return 1
    latency = calculate_latency(latency_raw, config)
    if latency.empty:
        print("Error: latency dataframe is empty after processing.", file=sys.stderr)
        return 1

    mem_latency_raw = parse_ptrchase_mem_latency(
        working_dir, measurement_dirs, config, stats_dir, stats_filename)
    if mem_latency_raw.empty:
        print("Error: unable to parse ptr-chase mem latency data.", file=sys.stderr)
        return 1

    agg_bw = aggregate_metric(bandwidth, "bandwidth_bytes_per_second")
    agg_lat_core = aggregate_metric(latency, "latency_core_ptr_chase")
    agg_lat_mem = aggregate_metric(mem_latency_raw, "latency_mem_ptr_chase")
    if agg_bw.empty or agg_lat_core.empty or agg_lat_mem.empty:
        print("Error: aggregation produced no data.", file=sys.stderr)
        return 1

    agg_lat_mem_ram = pd.DataFrame()
    agg_bw_mem_ram = pd.DataFrame()
    has_memory_view = False

    ram_latency_raw = parse_ramulator_mem_latency(
        working_dir, measurement_dirs, config)
    ram_bandwidth_raw = parse_ramulator_mem_bandwidth(
        working_dir, measurement_dirs, config)
    if not ram_latency_raw.empty and not ram_bandwidth_raw.empty:
        agg_lat_mem_ram = aggregate_metric(
            ram_latency_raw, "latency_mem_ptr_chase_ram")
        agg_bw_mem_ram = aggregate_metric(
            ram_bandwidth_raw, "bandwidth_ram")
        has_memory_view = not agg_lat_mem_ram.empty and not agg_bw_mem_ram.empty
    else:
        print("Info: memory-simulator view data unavailable, skipping backend plot.")

    merged = agg_bw.merge(
        agg_lat_core, on=["type", "rd_percentage", "pause"], how="inner")
    merged = merged.merge(
        agg_lat_mem, on=["type", "rd_percentage", "pause"], how="inner")
    if has_memory_view:
        merged = merged.merge(agg_lat_mem_ram, on=[
                              "type", "rd_percentage", "pause"], how="inner")
        merged = merged.merge(agg_bw_mem_ram, on=[
                              "type", "rd_percentage", "pause"], how="inner")
    if merged.empty:
        print("Error: merged dataset is empty.", file=sys.stderr)
        return 1

    processed_dir = os.path.join(output_dir, "processed")
    figures_dir = os.path.join(output_dir, "figures")
    os.makedirs(processed_dir, exist_ok=True)
    os.makedirs(figures_dir, exist_ok=True)

    csv_path = os.path.join(processed_dir, "bandwidth_latency.csv")
    merged.to_csv(csv_path, index=False)

    dfs_core = split_by_rd_percentage(merged, "latency_core_ptr_chase")
    dfs_mem = split_by_rd_percentage(merged, "latency_mem_ptr_chase")
    # New: Ramulator plot uses its own bandwidth column
    plot_core_path = os.path.join(figures_dir, "bandwidth_latency_zsim_core.pdf")
    plot_mem_path = os.path.join(figures_dir, "bandwidth_latency_zsim_mem.pdf")
    generate_plot(dfs_core, config, topology, plot_core_path,
                  "")
    generate_plot(dfs_mem, config, topology, plot_mem_path,
                  "")

    print(f"Wrote merged CSV to {csv_path}")
    print(f"Wrote core latency plot to {plot_core_path}")
    print(f"Wrote mem latency plot to {plot_mem_path}")
    if has_memory_view:
        dfs_mem_ram = split_by_rd_percentage(
            merged,
            latency_col="latency_mem_ptr_chase_ram",
            bandwidth_col="bandwidth_ram",
        )
        plot_mem_ram_path = os.path.join(
            figures_dir, "bandwidth_latency_ramulator.pdf")
        generate_plot(dfs_mem_ram, config, topology, plot_mem_ram_path,
                      "")
        print(f"Wrote ramulator latency plot to {plot_mem_ram_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
