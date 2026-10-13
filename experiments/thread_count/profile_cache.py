#!/usr/bin/env python3
"""Profile cache sectors for each world's fastest uninstrumented configuration."""

import argparse
import csv
import io
import shutil
import subprocess
from collections import defaultdict
from pathlib import Path

from sweep_worlds import gpu_memory, write_csv


METRICS = (
    "l1tex__t_sectors_lookup_hit.sum", "l1tex__t_sectors_lookup_miss.sum",
    "lts__t_sectors_lookup_hit.sum", "lts__t_sectors_lookup_miss.sum",
    "dram__bytes_read.sum", "dram__bytes_write.sum",
)


def parse_metrics(path, ticks):
    text = path.read_text()
    lines = text.splitlines()
    header = next((i for i, line in enumerate(lines)
                   if ('"Metric Name"' in line and '"Metric Value"' in line)
                   or (line.startswith('"ID",') and all(f'"{name}"' in line for name in METRICS))), None)
    if header is None:
        raise RuntimeError(f"no counter records in {path}; inspect profiler errors")
    values = defaultdict(list)
    for row in csv.DictReader(io.StringIO("\n".join(lines[header:]))):
        if "Metric Name" in row:
            name = row.get("Metric Name")
            if name in METRICS:
                values[name].append(float(row["Metric Value"].replace(",", "")))
        elif row.get("ID", "").isdigit():
            for name in METRICS:
                values[name].append(float(row[name].replace(",", "")))
    if any(len(values[name]) != ticks for name in METRICS):
        raise RuntimeError(f"expected {ticks} records per metric, got {dict((k, len(v)) for k, v in values.items())}")
    return {name: sum(values[name]) for name in METRICS}


def percentage(hits, misses):
    return 100 * hits / (hits + misses) if hits + misses else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("measurements", type=Path)
    parser.add_argument("--ncu", type=Path, default=Path(shutil.which("ncu") or "ncu"))
    parser.add_argument("--section-folder", type=Path)
    parser.add_argument("--binary", type=Path, default=Path("build/thread_count"))
    parser.add_argument("--ticks", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--output-dir", type=Path, help="default: cache/ beside the timing CSV")
    args = parser.parse_args()
    if args.ticks < 1 or args.warmup < 0:
        parser.error("ticks must be positive and warmup nonnegative")
    ncu, binary = args.ncu.resolve(), args.binary.resolve()
    if not ncu.is_file() or not binary.is_file():
        parser.error("ncu and benchmark executables must exist")
    if args.output_dir is None:
        args.output_dir = args.measurements.parent / "cache"
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with args.measurements.open(newline="") as source:
        measurements = [row for row in csv.DictReader(source) if row["access"] == "lookup"]
    groups = defaultdict(list)
    for row in measurements:
        if row["verified"] != "true":
            parser.error("profile only previously verified configurations")
        groups[tuple(row[field] for field in ("world_x", "world_y", "world_z", "materials"))].append(row)
    if not groups:
        parser.error("no verified lookup measurements")
    multiple_materials = len({key[3] for key in groups}) > 1
    summaries = []
    for world, rows in sorted(groups.items(), key=lambda item: tuple(map(int, item[0]))):
        best = min(rows, key=lambda row: (float(row["median_ms_per_tick"]), int(row["launched_threads"])))
        x_dim, y_dim, z_dim, materials = world
        case = f"{x_dim}x{y_dim}x{z_dim}"
        if multiple_materials:
            case += f"_m{materials}"
        one_per_tile = best["scheduler"] == "one_per_tile"
        kernel = f"regex:.*updateILb1ELb{int(one_per_tile)}.*"
        required = int(materials)**8 * 4 + int(best["tiles"]) * 8
        memory = gpu_memory(args.device)
        if required + 256 * 2**20 > memory["nvml_free_bytes"]:
            raise RuntimeError(f"insufficient current VRAM before profiling {case}")
        raw = args.output_dir / f"{case}_counters.csv"
        timing = args.output_dir / f"{case}_instrumented.csv"
        command = [str(ncu)]
        if args.section_folder:
            command += ["--section-folder", str(args.section_folder.resolve())]
        command += [
            "--kernel-name-base", "mangled", "--kernel-name", kernel,
            "--launch-skip", str(args.warmup), "--launch-count", str(args.ticks),
            "--metrics", ",".join(METRICS), "--replay-mode", "application",
            "--cache-control", "none", "--clock-control", "none",
            "--csv", "--page", "raw", "--log-file", str(raw),
            str(binary), "--world", x_dim, y_dim, z_dim,
            "--materials", materials, "--access", "lookup", "--launch", "direct",
            "--workers", best["requested_workers"], "--block-sizes", best["requested_block_size"],
            "--ticks", str(args.ticks), "--warmup", str(args.warmup), "--repeats", "1",
            "--device", str(args.device), "--output", str(timing),
        ]
        print(f"Profiling {case}: {best['scheduler']}, {best['launched_threads']} threads; "
              f"{args.ticks} warmed ticks, free {memory['nvml_free_bytes'] / 2**20:.0f} MiB.", flush=True)
        with (args.output_dir / f"{case}.log").open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=log)
        if result.returncode:
            raise RuntimeError(f"profiler exited {result.returncode}; see {raw} and {case}.log")
        totals = parse_metrics(raw, args.ticks)
        l1_hit, l1_miss = (totals[name] for name in METRICS[:2])
        l2_hit, l2_miss = (totals[name] for name in METRICS[2:4])
        summary = {
            "world_x": x_dim, "world_y": y_dim, "world_z": z_dim, "materials": materials,
            "tiles": best["tiles"], "unprofiled_median_ms_per_tick": best["median_ms_per_tick"],
            "launched_threads": best["launched_threads"], "scheduler": best["scheduler"],
            "block_size": best["threads_per_block"], "warmup_ticks": args.warmup,
            "profiled_ticks": args.ticks, "cache_control": "none", "replay_mode": "application",
            "l1_sector_hit_percent": percentage(l1_hit, l1_miss),
            "l2_sector_hit_percent": percentage(l2_hit, l2_miss),
            "l1_hit_sectors": l1_hit, "l1_miss_sectors": l1_miss,
            "l2_hit_sectors": l2_hit, "l2_miss_sectors": l2_miss,
            "dram_read_bytes_per_tick": totals[METRICS[4]] / args.ticks,
            "dram_write_bytes_per_tick": totals[METRICS[5]] / args.ticks,
            "nvml_used_bytes_before_profile": memory["nvml_used_bytes"],
            "nvml_free_bytes_before_profile": memory["nvml_free_bytes"],
            "estimated_required_bytes": required, "status": "measured",
        }
        summaries.append(summary)
        write_csv(args.output_dir / "cache_summary.csv", summaries)
        print(f"  L1 sector hits: {summary['l1_sector_hit_percent']:.2f}%; "
              f"L2 sector hits: {summary['l2_sector_hit_percent']:.2f}%.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
