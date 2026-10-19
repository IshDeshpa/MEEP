#!/usr/bin/env python3
"""Build plots and a self-contained comparison report from timing/counter CSVs."""

import argparse
import base64
import csv
import html
import json
import math
import os
import statistics
from collections import defaultdict
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", "/tmp/meep-transition-matplotlib")

CONFIGS = [("cube", "single"), ("face", "sequential"), ("face", "independent"),
           ("edge", "sequential"), ("edge", "independent")]
LABELS = ["Cube", "Face sequential", "Face independent", "Edge sequential", "Edge independent"]
COLORS = ["#243b53", "#007f86", "#50a8bf", "#c27816", "#d35b60"]


def number(value):
    try:
        result = float(value)
        return result if math.isfinite(result) else None
    except (TypeError, ValueError):
        return None


def enrich(row):
    result = dict(row)
    result["config"] = LABELS[CONFIGS.index((row["variant"], row["mode"]))]
    tiles = int(row["tiles"])
    result["tiles_millions"] = tiles / 1e6
    result["aggregate_ns_per_tile"] = float(row["median_ms_per_tick"]) * 1e6 / tiles
    for field in ["dram_read_bytes_per_tick", "dram_write_bytes_per_tick", "warp_instructions_per_tick",
                  "l1_hit_sectors_per_tick", "l1_miss_sectors_per_tick", "l2_hit_sectors_per_tick", "l2_miss_sectors_per_tick"]:
        value = number(row.get(field))
        result[field.replace("per_tick", "per_tile")] = value / tiles if value is not None else ""
    reads, writes = number(result.get("dram_read_bytes_per_tile")), number(result.get("dram_write_bytes_per_tile"))
    result["dram_total_bytes_per_tile"] = reads + writes if reads is not None and writes is not None else ""
    for cache in ("l1", "l2"):
        hit, miss = number(result.get(f"{cache}_hit_sectors_per_tile")), number(result.get(f"{cache}_miss_sectors_per_tile"))
        result[f"{cache}_total_sectors_per_tile"] = hit + miss if hit is not None and miss is not None else ""
    return result


def load_rows(path):
    with path.open(newline="") as source:
        rows = [enrich(row) for row in csv.DictReader(source)]
    seen = set()
    for row in rows:
        key = tuple(row[field] for field in ("world_x", "world_y", "world_z", "materials", "variant", "mode"))
        if key in seen:
            raise ValueError(f"duplicate comparison point: {key}")
        seen.add(key)
        x, y, z = (int(row[field]) for field in ("world_x", "world_y", "world_z"))
        if int(row["tiles"]) != (x // 2) * (y // 2) * (z // 2):
            raise ValueError("tile count disagrees with non-overlapping 2x2x2 geometry")
        if row["verified"] != "true" or row["timing_context"] != "uninstrumented":
            raise ValueError("report requires verified, uninstrumented source timings")
    return sorted(rows, key=lambda r: (int(r["materials"]), int(r["world_x"]), CONFIGS.index((r["variant"], r["mode"]))))


def make_plots(rows, directory):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    materials = sorted({int(r["materials"]) for r in rows})
    widths = sorted({int(r["world_x"]) for r in rows})
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10, "axes.spines.top": False,
                         "axes.spines.right": False, "axes.titleweight": "bold", "figure.facecolor": "white"})
    legend = [Line2D([0], [0], color=c, lw=2, marker="o", ms=4, label=l) for c, l in zip(COLORS, LABELS)]
    artifacts = []
    hardware = (f"{rows[0]['gpu']} · sm_{rows[0]['compute_capability'].replace('.', '')} · "
                f"block {rows[0]['threads_per_block']} · seed {rows[0]['seed']} · fixed partition · independent LUT loads volatile")

    def save(fig, name, title, subtitle):
        fig.suptitle(title, fontsize=17, fontweight="bold", y=.99)
        fig.text(.5, .945, subtitle, ha="center", fontsize=10, color="#526476")
        fig.legend(handles=legend, loc="lower center", bbox_to_anchor=(.5, -.005), ncol=5, frameon=False)
        fig.text(.5, -.045, hardware, ha="center", fontsize=8, color="#607487")
        fig.tight_layout(rect=[0, .055, 1, .925], h_pad=2)
        for extension in ("png", "svg", "pdf"):
            fig.savefig(directory / f"{name}.{extension}", dpi=180, bbox_inches="tight")
        plt.close(fig)
        artifacts.append((name, title))

    for name, field, title, ylabel, scale in [
        ("runtime", "median_ms_per_tick", "Whole-world runtime across world sizes", "Milliseconds per world tick", "linear"),
        ("instructions", "warp_instructions_per_tile", "Executed instructions per tile", "Warp instructions / tile", "log"),
        ("dram", "dram_read_bytes_per_tile", "DRAM reads per tile", "Measured DRAM read bytes / tile", "linear"),
        ("l1", "l1_sector_hit_percent", "L1/TEX sector hit rate", "Hit sectors / lookup sectors (%)", "linear"),
        ("l2", "l2_sector_hit_percent", "L2 sector hit rate", "Hit sectors / lookup sectors (%)", "linear"),
        ("l2_traffic", "l2_total_sectors_per_tile", "L2 sector traffic per tile", "L2 hit + miss sectors / tile", "linear"),
        ("occupancy", "achieved_occupancy_percent", "Achieved occupancy", "Active warps / maximum warps (%)", "linear"),
    ]:
        if not any(number(row.get(field)) is not None for row in rows):
            continue
        cols = min(2, len(materials))
        fig, axes = plt.subplots(math.ceil(len(materials) / cols), cols, figsize=(12, 8), squeeze=False, sharey=True)
        for ax, material in zip(axes.flat, materials):
            for config, label, color in zip(CONFIGS, LABELS, COLORS):
                series = [r for r in rows if int(r["materials"]) == material and (r["variant"], r["mode"]) == config]
                series = [r for r in series if number(r.get(field)) is not None]
                series.sort(key=lambda r: int(r["world_x"]))
                if not series:
                    continue
                x = [int(r["world_x"]) for r in series]
                y = [float(r[field]) for r in series]
                ax.plot(x, y, marker="o", ms=4, lw=2, color=color, label=label)
                if field == "median_ms_per_tick":
                    ax.fill_between(x, [float(r["min_ms_per_tick"]) for r in series],
                                    [float(r["max_ms_per_tick"]) for r in series], color=color, alpha=.12)
            ax.set_title(f"{material} materials")
            ax.set_xlabel("World X = Z (Y = 200)")
            ax.set_ylabel(ylabel)
            ax.set_xticks(widths)
            ax.set_yscale(scale)
            ax.grid(alpha=.18, which="both")
            if field.endswith("percent"):
                ax.set_ylim(-2, 102)
        if scale == "linear" and not field.endswith("percent"):
            # Shared axes must include every panel. Setting only the lower
            # limit in the first panel disables later upward autoscaling.
            bound_field = "max_ms_per_tick" if field == "median_ms_per_tick" else field
            maximum = max((float(row[bound_field]) for row in rows
                           if number(row.get(bound_field)) is not None), default=1)
            axes.flat[0].set_ylim(0, max(maximum * 1.06, 1e-9))
        for ax in list(axes.flat)[len(materials):]:
            ax.set_visible(False)
        subtitle = "One thread per 2×2×2 tile · same colors in every panel"
        if name == "runtime":
            subtitle += " · median of 5 samples, shaded sample range"
        elif name in ("l1", "l2"):
            subtitle += " · whole kernel: world buffers + LUT, including writes"
        elif name == "instructions":
            subtitle += " · hardware warp-instruction count, log scale"
        save(fig, name, title, subtitle)

    # Matched-condition heatmaps make the material/configuration axes explicit.
    largest = max(widths)
    lookup = {(int(r["world_x"]), int(r["materials"]), r["variant"], r["mode"]): r for r in rows}
    fig, axes = plt.subplots(1, 3, figsize=(16, 5))
    matrices = []
    for field, label in [("median_ms_per_tick", "Milliseconds per world tick"),
                         ("speedup", "Speedup relative to cube"),
                         ("material_ratio", f"Runtime relative to {min(materials)} materials")]:
        matrix = []
        for variant, mode in CONFIGS:
            line = []
            for material in materials:
                row = lookup[(largest, material, variant, mode)]
                ms = float(row["median_ms_per_tick"])
                if field == "speedup":
                    value = float(lookup[(largest, material, "cube", "single")]["median_ms_per_tick"]) / ms
                elif field == "material_ratio":
                    value = ms / float(lookup[(largest, min(materials), variant, mode)]["median_ms_per_tick"])
                else:
                    value = ms
                line.append(value)
            matrix.append(line)
        matrices.append((matrix, label))
    for ax, (matrix, label) in zip(axes, matrices):
        image = ax.imshow(matrix, aspect="auto", cmap="YlGnBu" if "Speedup" in label else "YlOrRd")
        ax.set_xticks(range(len(materials)), materials)
        ax.set_yticks(range(len(LABELS)), LABELS)
        ax.set_xlabel("Material count")
        ax.set_title(label, fontsize=11)
        threshold = min(min(x) for x in matrix) + .6 * (max(max(x) for x in matrix) - min(min(x) for x in matrix))
        for y, line in enumerate(matrix):
            for x, value in enumerate(line):
                ax.text(x, y, f"{value:.2f}" + ("×" if "relative" in label else ""), ha="center", va="center",
                        color="white" if value > threshold else "#243b53", fontsize=10)
        fig.colorbar(image, ax=ax, shrink=.72)
    fig.suptitle(f"Matched comparisons at {largest}×200×{largest}", fontsize=17, fontweight="bold")
    fig.text(.5, -.015, hardware, ha="center", fontsize=8, color="#607487")
    fig.tight_layout(rect=[0, 0, 1, .93])
    for extension in ("png", "svg", "pdf"):
        fig.savefig(directory / f"matched_comparisons.{extension}", dpi=180, bbox_inches="tight")
    plt.close(fig)
    artifacts.append(("matched_comparisons", "Matched comparisons at the largest world"))

    if not any(number(row.get("dram_read_bytes_per_tile")) is not None for row in rows):
        return artifacts
    fig, axes = plt.subplots(1, 3, figsize=(16, 5))
    for ax, field, title in zip(axes,
            ["l1_sector_hit_percent", "l2_sector_hit_percent", "dram_read_bytes_per_tile"],
            ["L1/TEX sector hits (%)", "L2 sector hits (%)", "DRAM read bytes / tile"]):
        matrix = [[number(lookup[(largest, material, *config)].get(field)) for material in materials]
                  for config in CONFIGS]
        matrix = [[value if value is not None else math.nan for value in line] for line in matrix]
        values = [v for line in matrix for v in line if math.isfinite(v)]
        maximum = 100 if field.endswith("percent") else max(values, default=1)
        image = ax.imshow(matrix, vmin=0, vmax=maximum, aspect="auto", cmap="viridis")
        ax.set_xticks(range(len(materials)), materials)
        ax.set_yticks(range(len(LABELS)), LABELS)
        ax.set_xlabel("Material count")
        ax.set_title(title, fontsize=11)
        for y, line in enumerate(matrix):
            for x, value in enumerate(line):
                ax.text(x, y, f"{value:.1f}" if math.isfinite(value) else "—", ha="center", va="center",
                        color="#607487" if not math.isfinite(value) else "#203346" if value > .65 * maximum else "white", fontsize=10)
        fig.colorbar(image, ax=ax, shrink=.72)
    fig.suptitle(f"Memory hierarchy at {largest}×200×{largest}", fontsize=17, fontweight="bold")
    fig.text(.5, .02, "Hardware counters for the full kernel: world buffers + LUT, including writes", ha="center", color="#607487")
    fig.text(.5, -.015, hardware, ha="center", fontsize=8, color="#607487")
    fig.tight_layout(rect=[0, .05, 1, .93])
    for extension in ("png", "svg", "pdf"):
        fig.savefig(directory / f"memory_summary.{extension}", dpi=180, bbox_inches="tight")
    plt.close(fig)
    artifacts.append(("memory_summary", "Which caches serve the work, and how much reaches DRAM"))
    return artifacts


def findings(rows):
    widths = sorted({int(r["world_x"]) for r in rows})
    materials = sorted({int(r["materials"]) for r in rows})
    largest, smallest = max(widths), min(widths)
    lookup = {(int(r["world_x"]), int(r["materials"]), r["variant"], r["mode"]): r for r in rows}
    result = []
    winners = []
    for m in materials:
        candidates = [lookup[(largest, m, *config)] for config in CONFIGS]
        best = min(candidates, key=lambda r: float(r["median_ms_per_tick"]))
        cube = lookup[(largest, m, "cube", "single")]
        winners.append(f"{m} materials: {best['config']} ({float(best['median_ms_per_tick']):.3f} ms, "
                       f"{float(cube['median_ms_per_tick'])/float(best['median_ms_per_tick']):.2f}× cube throughput)")
    result.append(f"At {largest}×200×{largest}, the fastest measured medians are " + "; ".join(winners) + ".")
    win_counts = defaultdict(int)
    for width in widths:
        for material in materials:
            best = min((lookup[(width, material, *config)] for config in CONFIGS),
                       key=lambda row: float(row["median_ms_per_tick"]))
            win_counts[best["config"]] += 1
    result[0] += f" Across all {len(widths) * len(materials)} world/material cases, fastest-median counts are " + "; ".join(
        f"{label}: {count}" for label, count in sorted(win_counts.items(), key=lambda item: -item[1])) + "."
    ratios = []
    for m in materials:
        for config in CONFIGS:
            ratios.append(float(lookup[(largest, m, *config)]["median_ms_per_tick"]) /
                          float(lookup[(smallest, m, *config)]["median_ms_per_tick"]))
    result.append(f"Growing X and Z from {smallest} to {largest} multiplies tile count by {(largest/smallest)**2:.2f}×. "
                  f"Measured tick-time ratios span {min(ratios):.2f}–{max(ratios):.2f}× across matched configurations/materials.")
    effects = []
    for config, label in zip(CONFIGS, LABELS):
        ratio = float(lookup[(largest, max(materials), *config)]["median_ms_per_tick"]) / \
                float(lookup[(largest, min(materials), *config)]["median_ms_per_tick"])
        effects.append(f"{label}: {ratio:.2f}×")
    result.append(f"At the largest world, changing {min(materials)} → {max(materials)} materials changes runtime by " + "; ".join(effects) + ".")
    if all(number(r.get("dram_read_bytes_per_tile")) is not None for r in rows):
        traffic = []
        instructions = []
        for config, label in zip(CONFIGS, LABELS):
            selected = [r for r in rows if (r["variant"], r["mode"]) == config]
            reads = [float(r["dram_read_bytes_per_tile"]) for r in selected]
            inst = [float(r["warp_instructions_per_tile"]) for r in selected if number(r.get("warp_instructions_per_tile")) is not None]
            traffic.append(f"{label}: {min(reads):.2f}–{max(reads):.2f} B/tile")
            if inst:
                instructions.append(f"{label}: {min(inst):.2f}–{max(inst):.2f}")
        result.append("Measured DRAM reads across the sweep: " + "; ".join(traffic) + ".")
        if instructions:
            result.append("Executed warp instructions per tile across the sweep: " + "; ".join(instructions) + ".")
        cube = lookup[(largest, max(materials), "cube", "single")]
        edge = lookup[(largest, max(materials), "edge", "sequential")]
        activity_fields = ["achieved_occupancy_percent", "issue_active_percent", "stall_long_scoreboard_percent"]
        if all(number(row.get(field)) is not None for row in (cube, edge) for field in activity_fields):
            result.append(f"At {max(materials)} materials and the largest world, cube and edge sequential have similar achieved "
                          f"occupancy ({float(cube['achieved_occupancy_percent']):.1f}% and "
                          f"{float(edge['achieved_occupancy_percent']):.1f}%), but issue-active percentages are "
                          f"{float(cube['issue_active_percent']):.2f}% and {float(edge['issue_active_percent']):.2f}%. "
                          f"Their long-scoreboard warp-state shares are {float(cube['stall_long_scoreboard_percent']):.1f}% "
                          f"and {float(edge['stall_long_scoreboard_percent']):.1f}%. High occupancy alone does not imply high throughput; "
                          "warp-state shares are not percentages of total kernel time.")
        endpoints = [lookup[(largest, material, *config)] for material in (min(materials), max(materials))
                     for config in [("face", "sequential"), ("edge", "independent")]]
        cache_fields = ["l1_sector_hit_percent", "l2_sector_hit_percent", "l2_total_sectors_per_tile"]
        if not all(number(row.get(field)) is not None for row in endpoints for field in cache_fields):
            return result
        low = lookup[(largest, min(materials), "face", "sequential")]
        high = lookup[(largest, max(materials), "face", "sequential")]
        result.append(f"For face sequential at the largest world, L1 hits change from {float(low['l1_sector_hit_percent']):.1f}% "
                      f"to {float(high['l1_sector_hit_percent']):.1f}% as materials rise {min(materials)} → {max(materials)}. "
                      f"Kernel L2 sector traffic rises from {float(low['l2_total_sectors_per_tile']):.2f} to "
                      f"{float(high['l2_total_sectors_per_tile']):.2f} sectors/tile while DRAM reads stay near "
                      f"{float(high['dram_read_bytes_per_tile']):.1f} B/tile.")
        low = lookup[(largest, min(materials), "edge", "independent")]
        high = lookup[(largest, max(materials), "edge", "independent")]
        result.append(f"For edge independent at the largest world, L2 hits increase {float(low['l2_sector_hit_percent']):.1f}% → "
                      f"{float(high['l2_sector_hit_percent']):.1f}%, but total L2 sectors/tile increase "
                      f"{float(low['l2_total_sectors_per_tile']):.2f} → {float(high['l2_total_sectors_per_tile']):.2f}. "
                      "The higher hit fraction coexists with more L2 traffic and slower steps; instructions and DRAM reads per tile stay nearly constant.")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    rows = load_rows(args.report)
    directory = args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "comparison_data.csv").open("w", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    summary = findings(rows)
    (directory / "findings.json").write_text(json.dumps(summary, indent=2) + "\n")
    artifacts = make_plots(rows, directory)
    template = (Path(__file__).parent / "report_template.html").read_text()
    ordering = ["runtime", "matched_comparisons", "memory_summary", "instructions", "dram", "l1", "l2", "l2_traffic", "occupancy"]
    artifacts.sort(key=lambda artifact: ordering.index(artifact[0]))
    def image_section(name, title):
        return (f'<section class="card"><h2>{html.escape(title)}</h2><img alt="{html.escape(title)}" '
                       f'src="data:image/png;base64,{base64.b64encode((directory / (name + ".png")).read_bytes()).decode()}">'
                       f'<p><a href="{name}.svg">SVG</a> · <a href="{name}.pdf">PDF</a></p></section>')
    images = "\n".join(image_section(name, title) for name, title in artifacts[:4])
    if len(artifacts) > 4:
        images += '<details class="card"><summary>More world-size comparisons: traffic, caches, and occupancy</summary>'
        images += "\n".join(image_section(name, title) for name, title in artifacts[4:]) + '</details>'
    content = template.replace("__DATA__", json.dumps(rows).replace("</", "<\\/")) \
        .replace("__FINDINGS__", "".join(f"<li>{html.escape(item)}</li>" for item in summary[:3])) \
        .replace("__COUNTER_FINDINGS__", "".join(f"<li>{html.escape(item)}</li>" for item in summary[3:])) \
        .replace("__FIGURES__", images).replace("__CASE_COUNT__", str(len(rows))) \
        .replace("__MEASURED_COUNT__", str(sum(r.get("profile_status") == "measured" for r in rows)))
    (directory / "report.html").write_text(content)
    print(f"Saved comparison report: {directory / 'report.html'}")
    for item in summary:
        print(item)


if __name__ == "__main__":
    main()
