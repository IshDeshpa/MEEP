#!/usr/bin/env python3
"""Plot a thread_count CSV. Requires matplotlib only when rendering."""

import argparse
import csv
from collections import defaultdict
from pathlib import Path


def plot_workload(axis, rows, access, include_access=False):
    groups = defaultdict(list)
    for row in rows:
        if row["access"] == access:
            groups[int(row["requested_block_size"])].append(row)
    prefix = f"{access}: " if include_access else ""
    for block, measurements in sorted(groups.items()):
        grid = sorted(
            (row for row in measurements if row["scheduler"] == "grid_stride"),
            key=lambda row: int(row["launched_threads"]),
        )
        color = None
        if grid:
            x_values = [int(row["launched_threads"]) for row in grid]
            medians = [float(row["median_ms_per_tick"]) for row in grid]
            line, = axis.plot(x_values, medians, ".-", label=f"{prefix}grid stride, block {block}")
            color = line.get_color()
            axis.fill_between(
                x_values,
                [float(row["min_ms_per_tick"]) for row in grid],
                [float(row["max_ms_per_tick"]) for row in grid],
                color=color, alpha=0.12,
            )
            # Tiny grids use a smaller actual block. The reference line
            # uses the resident capacity for the requested full block size.
            full_blocks = [row for row in grid if int(row["launched_threads"]) >= block]
            if full_blocks:
                capacity = int(full_blocks[0]["resident_thread_capacity"])
                axis.axvline(capacity, color=color, linestyle=":", alpha=0.6)
        tile_rows = [row for row in measurements if row["scheduler"] == "one_per_tile"]
        if tile_rows:
            axis.scatter(
                [int(row["launched_threads"]) for row in tile_rows],
                [float(row["median_ms_per_tick"]) for row in tile_rows],
                color=color, marker="X", s=70, label=f"{prefix}one per tile, block {block}",
            )


def hardware_core_count(rows, override):
    if override is not None:
        if override <= 0:
            raise ValueError("hardware core count must be positive")
        return override
    # NVIDIA's Turing tuning guide specifies 64 FP32/SP cores per SM.
    # Do not infer another architecture's core count from its thread capacity.
    if all(row["compute_capability"] == "7.5" for row in rows):
        counts = {int(row["sm_count"]) * 64 for row in rows}
        if len(counts) == 1:
            return counts.pop()
    raise ValueError("unknown hardware SP count; specify --hardware-cores")


def fastest_annotation(axis, rows):
    best = min(rows, key=lambda row: (float(row["median_ms_per_tick"]), int(row["launched_threads"])))
    threads, milliseconds = int(best["launched_threads"]), float(best["median_ms_per_tick"])
    axis.scatter([threads], [milliseconds], marker="*", s=140, color="black", zorder=5)
    axis.annotate(
        f"Fastest median: {milliseconds:.6g} ms/tick\n{threads:,} threads ({best['scheduler']})",
        xy=(threads, milliseconds), xytext=(-170, 38), textcoords="offset points",
        fontsize=9, bbox={"facecolor": "white", "alpha": 0.9, "edgecolor": "0.8"},
        arrowprops={"arrowstyle": "->", "color": "0.3"},
    )


def format_axis(axis, title, core_count, rows):
    axis.axvline(core_count, color="black", linestyle="--", alpha=0.65,
                 label=f"Hardware SP count: {core_count:,}")
    fastest_annotation(axis, rows)
    axis.set_xscale("log", base=2)
    axis.set_yscale("log")
    axis.set_xlabel("Launched CUDA threads")
    axis.set_ylabel("Median milliseconds per complete tick")
    axis.set_title(title)
    axis.grid(True, which="both", alpha=0.2)
    axis.legend(fontsize="small")


def draw_cache_table(axis, cache_rows):
    axis.axis("off")
    cells = []
    for row in sorted(cache_rows, key=lambda row: int(row["world_x"])):
        cells.append([
            f"{row['world_x']}×{row['world_y']}×{row['world_z']}",
            f"{float(row['unprofiled_median_ms_per_tick']):.4f}",
            f"{int(row['launched_threads']):,}",
            f"{float(row['l1_sector_hit_percent']):.4f}%",
            f"{float(row['l2_sector_hit_percent']):.2f}%",
            f"{int(row['estimated_required_bytes']) / 2**20:.2f}",
        ])
    table = axis.table(
        cellText=cells,
        colLabels=["World", "Best ms/tick", "Threads", "L1 sector hits", "L2 sector hits", "VRAM MiB"],
        cellLoc="center", bbox=[0, 0, 1, 0.88],
    )
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    axis.set_title(
        "Time: uninstrumented medians. Cache: ten warmed ticks at each fastest configuration; buffers + LUT.",
        fontsize=10, pad=12,
    )


def draw_world_comparison(axis, selected, core_count, title):
    groups = defaultdict(list)
    for row in selected:
        key = tuple(int(row[field]) for field in ("world_x", "world_y", "world_z", "requested_block_size"))
        groups[key].append(row)
    for (x_dim, y_dim, z_dim, block), measurements in sorted(groups.items()):
        grid = sorted((row for row in measurements if row["scheduler"] == "grid_stride"),
                      key=lambda row: int(row["launched_threads"]))
        best = min(float(row["median_ms_per_tick"]) for row in measurements)
        line, = axis.plot(
            [int(row["launched_threads"]) for row in grid],
            [float(row["median_ms_per_tick"]) for row in grid], ".-",
            label=f"{x_dim}×{y_dim}×{z_dim}, block {block} (best {best:.6g} ms)",
        )
        color = line.get_color()
        axis.fill_between(
            [int(row["launched_threads"]) for row in grid],
            [float(row["min_ms_per_tick"]) for row in grid],
            [float(row["max_ms_per_tick"]) for row in grid], color=color, alpha=0.08,
        )
        per_tile = [row for row in measurements if row["scheduler"] == "one_per_tile"]
        axis.scatter([int(row["launched_threads"]) for row in per_tile],
                     [float(row["median_ms_per_tick"]) for row in per_tile],
                     marker="X", color=color, s=65)
    capacities = {int(row["resident_thread_capacity"]) for row in selected
                  if row["threads_per_block"] == row["requested_block_size"]}
    for capacity in sorted(capacities):
        axis.axvline(capacity, color="0.5", linestyle=":", alpha=0.7,
                     label=f"Estimated resident capacity: {capacity:,}")
    format_axis(axis, title, core_count, selected)


def plot_world_comparison(plt, rows, access, core_count, cache_rows=None):
    selected = [row for row in rows if row["access"] == access]
    if cache_rows:
        figure = plt.figure(figsize=(13, 9), layout="constrained")
        layout = figure.add_gridspec(2, 1, height_ratios=[4, 1.1])
        axis = figure.add_subplot(layout[0])
        draw_cache_table(figure.add_subplot(layout[1]), cache_rows)
    else:
        figure, axis = plt.subplots(figsize=(13, 7), layout="constrained")
    draw_world_comparison(axis, selected, core_count,
                          "Total tick time: read current state → load table[state] → write result")
    first = selected[0]
    figure.suptitle(
        f"{first['gpu']} | {first['materials']} materials | {first['launch_mode']} launches\n"
        "Each tick updates every neighborhood; X markers: one thread per tile; shading: sample min–max"
    )
    return figure


def plot_material_comparison(plt, rows, access, core_count, cache_rows=None):
    materials = sorted({int(row["materials"]) for row in rows if row["access"] == access})
    # Each material count gets its own chart and cache table, keeping the five
    # world-size curves readable instead of overlaying fifteen similar curves.
    figure = plt.figure(figsize=(14, 7 * len(materials)), layout="constrained")
    ratios = [ratio for _ in materials for ratio in ([4, 1.1] if cache_rows else [4])]
    layout = figure.add_gridspec(len(ratios), 1, height_ratios=ratios)
    for index, material in enumerate(materials):
        selected = [row for row in rows if row["access"] == access and int(row["materials"]) == material]
        row_index = index * (2 if cache_rows else 1)
        axis = figure.add_subplot(layout[row_index])
        draw_world_comparison(axis, selected, core_count,
                              f"{material} materials | LUT {int(selected[0]['lut_bytes']) / 2**30:.3f} GiB")
        if cache_rows:
            draw_cache_table(figure.add_subplot(layout[row_index + 1]),
                             [row for row in cache_rows if int(row["materials"]) == material])
    first = rows[0]
    figure.suptitle(
        f"{first['gpu']} | Total tick time: read current → load table[state] → write result\n"
        "One tick updates every neighborhood; X: one thread per tile; shading: sample min–max"
    )
    return figure


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, default=Path("results/latest.png"))
    parser.add_argument("--compare-worlds", action="store_true", help="compare world sizes on one graph")
    parser.add_argument("--compare-materials", action="store_true", help="one world-comparison panel per material count")
    parser.add_argument("--cache-summary", type=Path, help="add cache table beneath the world-comparison graph")
    parser.add_argument("--hardware-cores", type=int, help="override hardware SP count; auto-detected for Turing")
    parser.add_argument(
        "--access", choices=("lookup", "copy", "both"), default="lookup",
        help="workload to plot; copy is an optional buffer-only control (default: lookup)",
    )
    args = parser.parse_args()
    with args.csv.open(newline="") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        parser.error("CSV has no measurements")
    required = {
        "gpu", "compute_capability", "sm_count", "tiles", "world_x", "world_y", "world_z", "materials", "access",
        "scheduler", "requested_block_size", "launched_threads", "median_ms_per_tick",
        "min_ms_per_tick", "max_ms_per_tick", "resident_thread_capacity", "launch_mode",
    }
    if not required.issubset(rows[0]):
        parser.error("CSV is missing benchmark columns")
    run_fields = ("gpu", "launch_mode")
    if not args.compare_materials:
        run_fields += ("materials",)
    if not (args.compare_worlds or args.compare_materials):
        run_fields += ("world_x", "world_y", "world_z")
    if len({tuple(row[key] for key in run_fields) for row in rows}) != 1:
        parser.error("plot one GPU/launch configuration; use --compare-worlds or --compare-materials for several cases")
    try:
        core_count = hardware_core_count(rows, args.hardware_cores)
    except ValueError as error:
        parser.error(str(error))
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        parser.error("plotting requires matplotlib: python3 -m pip install matplotlib")

    selected = ("lookup", "copy") if args.access == "both" else (args.access,)
    modes = [mode for mode in selected if any(row["access"] == mode for row in rows)]
    if not modes:
        parser.error(f"CSV has no measurements for --access {args.access}")
    if args.compare_worlds or args.compare_materials:
        if len(modes) != 1:
            parser.error("--compare-worlds requires one workload: --access lookup or copy")
        cache_rows = None
        if args.cache_summary:
            with args.cache_summary.open(newline="") as source:
                cache_rows = list(csv.DictReader(source))
            measured_worlds = {tuple(row[field] for field in ("world_x", "world_y", "world_z", "materials"))
                               for row in rows if row["access"] == modes[0]}
            cache_worlds = {tuple(row[field] for field in ("world_x", "world_y", "world_z", "materials"))
                            for row in cache_rows}
            if modes[0] != "lookup" or measured_worlds != cache_worlds:
                parser.error("cache summary must match the lookup worlds in the graph")
        renderer = plot_material_comparison if args.compare_materials else plot_world_comparison
        figure = renderer(plt, rows, modes[0], core_count, cache_rows)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        figure.savefig(args.output, dpi=180)
        plt.close(figure)
        print(f"Wrote {args.output}")
        return
    if args.cache_summary:
        parser.error("--cache-summary requires --compare-worlds or --compare-materials")
    titles = {
        "lookup": "Total tick time: read current state → load table[current state] → write result",
        "copy": "Buffer-only copy control: world read → next-buffer write (no LUT)",
    }
    if len(modes) == 1:
        figure, axis = plt.subplots(figsize=(12, 6), layout="constrained")
        plot_workload(axis, rows, modes[0])
        format_axis(axis, titles[modes[0]], core_count,
                    [row for row in rows if row["access"] == modes[0]])
    else:
        figure = plt.figure(figsize=(14, 10), layout="constrained")
        layout = figure.add_gridspec(2, len(modes))
        total = figure.add_subplot(layout[0, :])
        for access in modes:
            plot_workload(total, rows, access, include_access=True)
        format_axis(total, "Total tick time: dependent lookup update vs buffer-only copy control",
                    core_count, [row for row in rows if row["access"] in modes])
        for index, access in enumerate(modes):
            axis = figure.add_subplot(layout[1, index])
            plot_workload(axis, rows, access)
            format_axis(axis, titles[access], core_count,
                        [row for row in rows if row["access"] == access])
    first = rows[0]
    tick_caption = f"One tick updates all {int(first['tiles']):,} neighborhoods once"
    if len(modes) > 1:
        tick_caption += "; curves are separate workloads"
    figure.suptitle(
        f"{first['gpu']} | {first['world_x']}×{first['world_y']}×{first['world_z']} "
        f"| {first['materials']} materials | {first['launch_mode']} launches\n"
        f"{tick_caption}\n"
        "Shading: sample min–max; dashed black: hardware SP count; dotted: resident thread capacity"
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(args.output, dpi=180)
    plt.close(figure)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
