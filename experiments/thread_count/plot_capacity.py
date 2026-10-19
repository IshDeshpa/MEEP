#!/usr/bin/env python3
"""Plot a one-thread-per-neighborhood world-size sweep and optional cache table."""

import argparse
import csv
import math
from pathlib import Path

from plot import draw_cache_table, hardware_core_count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--cache-summary", type=Path)
    parser.add_argument("--target-fps", type=float, default=60)
    parser.add_argument("--hardware-cores", type=int)
    parser.add_argument("--output", type=Path, default=Path("results/capacity_8_height_200/tick_times.png"))
    args = parser.parse_args()
    if not math.isfinite(args.target_fps) or args.target_fps <= 0:
        parser.error("target FPS must be positive and finite")
    with args.csv.open(newline="") as source:
        rows = sorted(csv.DictReader(source), key=lambda row: int(row["world_x"]))
    if not rows or any(row["scheduler"] != "one_per_tile" or row["access"] != "lookup"
                       or row["world_x"] != row["world_y"] or row["verified"] != "true" for row in rows):
        parser.error("expected verified square-world, one_per_tile lookup measurements")
    fields = ("gpu", "compute_capability", "world_z", "materials", "launch_mode",
              "requested_block_size", "warmup_ticks", "timed_ticks", "repeats")
    if len({tuple(row[key] for key in fields) for row in rows}) != 1:
        parser.error("plot one GPU/material/height/timing configuration")
    core_count = hardware_core_count(rows, args.hardware_cores)
    cache_rows = None
    if args.cache_summary:
        with args.cache_summary.open(newline="") as source:
            cache_rows = list(csv.DictReader(source))
        key_fields = ("world_x", "world_y", "world_z", "materials")
        if {tuple(row[k] for k in key_fields) for row in rows} != {
            tuple(row[k] for k in key_fields) for row in cache_rows
        }:
            parser.error("cache summary must match the measured worlds")
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter

    if cache_rows:
        figure = plt.figure(figsize=(14, 11), layout="constrained")
        layout = figure.add_gridspec(2, 1, height_ratios=[4, 2])
        axis = figure.add_subplot(layout[0])
        draw_cache_table(figure.add_subplot(layout[1]), cache_rows)
    else:
        figure, axis = plt.subplots(figsize=(13, 7), layout="constrained")
    widths = [int(row["world_x"]) for row in rows]
    times = [float(row["median_ms_per_tick"]) for row in rows]
    axis.plot(widths, times, "o-", label="Median complete tick time")
    axis.fill_between(widths, [float(row["min_ms_per_tick"]) for row in rows],
                      [float(row["max_ms_per_tick"]) for row in rows], alpha=0.15,
                      label="Sample min–max")
    budget_ms = 1000 / args.target_fps
    axis.axhline(budget_ms, color="tab:red", linestyle="--",
                 label=f"{args.target_fps:g} ticks/s budget: {budget_ms:.3f} ms")
    # One active thread per tile: width² * height / 8. The secondary axis
    # shows active threads; at most block_size-1 extra launched lanes are idle.
    factor = int(rows[0]["world_z"]) / 8
    to_threads = lambda width: width**2 * factor
    to_width = lambda threads: (threads / factor)**0.5
    axis.axvline(to_width(core_count), color="black", linestyle="--", alpha=0.65,
                 label=f"Hardware SP count: {core_count:,} (top thread axis)")
    capacity = int(rows[0]["resident_thread_capacity"])
    axis.axvline(to_width(capacity), color="0.5", linestyle=":",
                 label=f"Estimated resident capacity: {capacity:,} threads (top axis)")
    axis.set_xlim(0, widths[-1] + 100)
    axis.set_ylim(bottom=0)
    axis.set_xticks([0, *widths])
    top = axis.secondary_xaxis("top", functions=(to_threads, to_width))
    top.set_xlabel("Neighborhoods / active CUDA threads (one per neighborhood)")
    top.xaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{value / 1e6:g}M"))
    best = min(rows, key=lambda row: float(row["median_ms_per_tick"]))
    best_width, best_time = int(best["world_x"]), float(best["median_ms_per_tick"])
    axis.scatter([best_width], [best_time], marker="*", s=140, color="black", zorder=5)
    axis.annotate(f"Fastest median: {best_time:.4f} ms/tick\n{best_width}×{best_width}×{best['world_z']}",
                  xy=(best_width, best_time), xytext=(35, 75), textcoords="offset points",
                  bbox={"facecolor": "white", "alpha": 0.9, "edgecolor": "0.8"},
                  arrowprops={"arrowstyle": "->", "color": "0.3"})
    crossing = next((row for row in rows if float(row["median_ms_per_tick"]) >= budget_ms), None)
    if crossing:
        width, milliseconds = int(crossing["world_x"]), float(crossing["median_ms_per_tick"])
        axis.scatter([width], [milliseconds], s=90, color="tab:red", zorder=5)
        axis.annotate(f"First median ≥ budget: {width}×{width}×{crossing['world_z']}\n"
                      f"{milliseconds:.4f} ms/tick ({1000 / milliseconds:.2f} ticks/s)",
                      xy=(width, milliseconds), xytext=(-220, -70), textcoords="offset points",
                      bbox={"facecolor": "white", "alpha": 0.9, "edgecolor": "0.8"},
                      arrowprops={"arrowstyle": "->", "color": "tab:red"})
    axis.set_xlabel("World width and length (voxels): X×X×height")
    axis.set_ylabel("Milliseconds per complete tick")
    axis.grid(True, alpha=0.2)
    axis.legend(loc="upper left", fontsize=9, framealpha=1)
    first = rows[0]
    figure.suptitle(f"{first['gpu']} | {first['materials']} materials | {first['world_z']} voxels high\n"
                   f"Read current → load table[state] → write next | one thread per tile | block {first['threads_per_block']}\n"
                   f"{first['repeats']} samples × {first['timed_ticks']} ticks; allocations, setup and verification excluded")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(args.output, dpi=180)
    plt.close(figure)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
