#!/usr/bin/env python3
"""Increase square world size until a one-thread-per-tile tick exceeds its budget."""

import argparse
import csv
import math
import subprocess
from datetime import datetime, timezone
from pathlib import Path

from sweep_worlds import gpu_memory, write_csv


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/thread_count"))
    parser.add_argument("--start", type=int, default=100)
    parser.add_argument("--step", type=int, default=100)
    parser.add_argument("--height", type=int, default=200)
    parser.add_argument("--materials", type=int, choices=range(2, 17), default=8)
    parser.add_argument("--target-fps", type=float, default=60)
    parser.add_argument("--ticks", type=int, default=30)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--block-size", type=int, default=256)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--headroom-mib", type=int, default=256)
    parser.add_argument("--output-dir", type=Path, default=Path("results/capacity_8_height_200"))
    args = parser.parse_args()
    if any(value <= 0 or value % 2 for value in (args.start, args.step, args.height)):
        parser.error("start, step, and height must be positive and even")
    if not math.isfinite(args.target_fps) or args.target_fps <= 0:
        parser.error("target FPS must be positive and finite")
    if min(args.ticks, args.repeats, args.block_size) < 1 or args.block_size > 1024:
        parser.error("ticks/repeats must be positive and block size between 1 and 1024")
    if min(args.warmup, args.device, args.headroom_mib) < 0:
        parser.error("warmup, device, and headroom must be nonnegative")
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"benchmark binary not found: {binary}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    budget_ms = 1000 / args.target_fps
    measurements, memory_rows, summaries = [], [], []
    size = args.start
    while True:
        tiles = (size // 2) ** 2 * (args.height // 2)
        lut_bytes = args.materials**8 * 4
        required = lut_bytes + tiles * 8
        case = f"{size}x{size}x{args.height}"
        output = args.output_dir / f"{case}.csv"
        memory = {
            "checked_at_utc": datetime.now(timezone.utc).isoformat(),
            "world_x": size, "world_y": size, "world_z": args.height,
            "materials": args.materials, "tiles": tiles, "lut_bytes": lut_bytes,
            "world_buffers_bytes": tiles * 8, "estimated_required_bytes": required,
            "headroom_bytes": args.headroom_mib * 2**20, **gpu_memory(args.device),
            "cuda_total_vram_bytes_before_alloc": "",
            "cuda_free_vram_bytes_before_alloc": "",
            "cuda_used_vram_bytes_before_alloc": "",
            "status": "", "measurement_csv": output.name, "detail": "",
        }
        memory_rows.append(memory)
        print(f"{case}: {tiles:,} neighborhoods; need {required / 2**20:.2f} MiB + "
              f"{args.headroom_mib} MiB headroom; free {memory['nvml_free_bytes'] / 2**20:.0f} MiB, "
              f"used {memory['nvml_used_bytes'] / 2**20:.0f} MiB.", flush=True)
        if required + memory["headroom_bytes"] > memory["nvml_free_bytes"]:
            memory["status"] = "insufficient_vram"
            memory["detail"] = "estimate plus headroom exceeds current free VRAM; target not reached"
            write_csv(args.output_dir / "memory_checks.csv", memory_rows)
            raise RuntimeError(memory["detail"])
        command = [
            str(binary), "--world", str(size), str(size), str(args.height),
            "--materials", str(args.materials), "--access", "lookup",
            "--scheduler", "one_per_tile", "--launch", "graph",
            "--ticks", str(args.ticks), "--warmup", str(args.warmup),
            "--repeats", str(args.repeats), "--block-sizes", str(args.block_size),
            "--device", str(args.device), "--output", str(output),
        ]
        with (args.output_dir / f"{case}.log").open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=log)
        if result.returncode:
            memory["status"] = "failed"
            memory["detail"] = f"benchmark exited {result.returncode}; see {case}.log"
            write_csv(args.output_dir / "memory_checks.csv", memory_rows)
            raise RuntimeError(memory["detail"])
        with output.open(newline="") as source:
            rows = list(csv.DictReader(source))
        if len(rows) != 1:
            raise RuntimeError(f"expected exactly one scheduler/block result for {case}")
        row = rows[0]
        if row["verified"] != "true" or row["access"] != "lookup" or row["scheduler"] != "one_per_tile":
            raise RuntimeError(f"unexpected or unverified result for {case}")
        if int(row["required_vram_bytes"]) != required or int(row["tiles"]) != tiles:
            raise RuntimeError(f"CUDA and host size estimates disagree for {case}")
        for field in ("cuda_total_vram_bytes_before_alloc", "cuda_free_vram_bytes_before_alloc",
                      "cuda_used_vram_bytes_before_alloc"):
            memory[field] = row[field]
        memory["status"] = "passed"
        milliseconds = float(row["median_ms_per_tick"])
        reached = milliseconds >= budget_ms
        summaries.append({
            "world_x": size, "world_y": size, "world_z": args.height,
            "materials": args.materials, "tiles": tiles,
            "launched_threads": row["launched_threads"], "required_vram_bytes": required,
            "median_ms_per_tick": milliseconds, "ticks_per_second": 1000 / milliseconds,
            "min_ms_per_tick": row["min_ms_per_tick"], "max_ms_per_tick": row["max_ms_per_tick"],
            "target_ticks_per_second": args.target_fps, "target_ms_per_tick": budget_ms,
            "stop_threshold_reached": reached,
        })
        measurements.append(row)
        write_csv(args.output_dir / "measurements.csv", measurements)
        write_csv(args.output_dir / "memory_checks.csv", memory_rows)
        write_csv(args.output_dir / "summary.csv", summaries)
        print(f"  Verified: {milliseconds:.6f} ms/tick, {1000 / milliseconds:.2f} ticks/s; "
              f"sample range {float(row['min_ms_per_tick']):.6f}–{float(row['max_ms_per_tick']):.6f} ms.", flush=True)
        if reached:
            print(f"Stopped at the first median ≥ {budget_ms:.6f} ms/tick: {case}.", flush=True)
            break
        size += args.step
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
