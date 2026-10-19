#!/usr/bin/env python3
"""Measure X×200×X worlds with five update variants and live VRAM checks."""

import argparse
import csv
import hashlib
import json
import random
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

from profile_counters import gpu_memory, write_csv


def expected_case(rows, width, height, materials, args):
    variants = {("cube", "single"), ("face", "independent"), ("face", "sequential"),
                ("edge", "independent"), ("edge", "sequential")}
    if {(r.get("variant"), r.get("mode")) for r in rows} != variants or len(rows) != 5:
        return False
    fields = {"world_x": width, "world_y": height, "world_z": width, "materials": materials,
              "tiles": (width // 2)**2 * (height // 2), "seed": args.seed,
              "threads_per_block": args.block_size, "warmup_ticks": args.warmup,
              "timed_ticks": args.ticks, "repeats": args.repeats, "device_index": args.device,
              "preheat_ms": args.preheat_ms}
    return all(r.get("verified") == "true" and r.get("timing_context") == "uninstrumented"
               and r.get("scheduler") == "one_per_tile" and r.get("launch_mode") == "graph"
               and all(r.get(field) == str(value) for field, value in fields.items()) for r in rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path(__file__).parent / "build/transition_variations")
    parser.add_argument("--sizes", type=int, nargs="+", default=list(range(500, 1001, 100)))
    parser.add_argument("--height", type=int, default=200)
    parser.add_argument("--materials", type=int, nargs="+", default=[8, 10, 12, 14])
    parser.add_argument("--ticks", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--repeats", type=int, default=5)
    parser.add_argument("--block-size", type=int, default=256)
    parser.add_argument("--preheat-ms", type=int, default=500)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--headroom-mib", type=int, default=256)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if any(x < 2 or x % 2 for x in [*args.sizes, args.height]):
        parser.error("dimensions must be positive and even")
    if any(m < 2 or m > 16 for m in args.materials):
        parser.error("materials must be in 2..16")
    if min(args.ticks, args.repeats, args.block_size) < 1 or max(args.warmup, args.device, args.headroom_mib) < 0:
        parser.error("invalid timing/device/memory arguments")
    if args.warmup < 0 or args.device < 0 or args.headroom_mib < 0 or args.block_size > 1024:
        parser.error("invalid warmup/device/headroom/block size")
    binary, directory = args.binary.resolve(), args.output_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    context_path = directory / "run_context.json"
    if args.resume and context_path.exists():
        old = json.loads(context_path.read_text())
        if old["binary_sha256"] != digest:
            parser.error("benchmark binary changed; choose a new output directory")
    cases = [(w, m) for m in args.materials for w in args.sizes]
    random.Random(args.seed).shuffle(cases)
    context = {"started_utc": datetime.now(timezone.utc).isoformat(), "binary": str(binary),
               "binary_sha256": digest, "sizes": args.sizes, "height": args.height,
               "materials": args.materials, "ticks": args.ticks, "warmup": args.warmup,
               "repeats": args.repeats, "block_size": args.block_size, "seed": args.seed,
               "device": args.device, "case_order": cases, "cpu_reference_workers": 8,
               "timing_launch": "graph", "partition": "fixed", "independent_lut_loads": "volatile",
               "preheat_ms": args.preheat_ms}
    if not args.resume or not context_path.exists():
        context_path.write_text(json.dumps(context, indent=2) + "\n")
    memory_rows, measurements = [], []
    memory_path = directory / "memory_checks.csv"
    if args.resume and memory_path.exists():
        memory_rows = list(csv.DictReader(memory_path.open()))
    for order, (width, materials) in enumerate(cases):
        case = f"{width}x{args.height}x{width}_m{materials}"
        output = directory / f"{case}.csv"
        if args.resume and output.exists():
            rows = list(csv.DictReader(output.open()))
            if expected_case(rows, width, args.height, materials, args):
                measurements.extend(rows)
                write_csv(directory / "measurements.csv", measurements)
                print(f"Reused {case}: 5 verified variants", flush=True)
                continue
        tiles = (width // 2)**2 * (args.height // 2)
        worlds = tiles * 8
        required = materials**8 * 4 + worlds
        memory = {"case_order": order, "case": case, "checked_at_utc": datetime.now(timezone.utc).isoformat(),
                  "world_x": width, "world_y": args.height, "world_z": width, "tiles": tiles,
                  "materials": materials, "world_buffers_bytes": worlds, "cube_lut_bytes": materials**8 * 4,
                  "face_lut_bytes": materials**4 * 2, "edge_lut_bytes": materials**2,
                  "largest_required_bytes": required, "headroom_bytes": args.headroom_mib * 2**20,
                  **gpu_memory(args.device), "status": "preflight", "elapsed_seconds": ""}
        memory_rows.append(memory)
        write_csv(memory_path, memory_rows)
        if required + memory["headroom_bytes"] > memory["profile_nvml_free_bytes"]:
            memory["status"] = "insufficient_vram"
            write_csv(memory_path, memory_rows)
            raise RuntimeError(f"insufficient current VRAM for {case}")
        print(f"[{order+1}/{len(cases)}] {case}: {tiles:,} tiles; need {required/2**20:.1f} MiB + "
              f"{args.headroom_mib} MiB; free {memory['profile_nvml_free_bytes']/2**20:.0f} MiB", flush=True)
        command = [str(binary), "--world", str(width), str(args.height), str(width),
                   "--materials", str(materials), "--ticks", str(args.ticks), "--warmup", str(args.warmup),
                   "--repeats", str(args.repeats), "--block-size", str(args.block_size),
                   "--preheat-ms", str(args.preheat_ms),
                   "--seed", str(args.seed), "--device", str(args.device), "--output", str(output)]
        (directory / f"{case}_command.json").write_text(json.dumps(command, indent=2) + "\n")
        start = time.monotonic()
        with (directory / f"{case}.log").open("w") as log:
            result = subprocess.run(command, stdout=log, stderr=log)
        memory["elapsed_seconds"] = time.monotonic() - start
        memory["status"] = "failed" if result.returncode else "passed"
        write_csv(memory_path, memory_rows)
        if result.returncode:
            raise RuntimeError(f"{case} failed; see its log")
        rows = list(csv.DictReader(output.open()))
        if not expected_case(rows, width, args.height, materials, args):
            raise RuntimeError(f"incomplete or unverified results for {case}")
        measurements.extend(rows)
        write_csv(directory / "measurements.csv", measurements)
        print(f"  Passed 5 variants in {memory['elapsed_seconds']:.1f}s; " + "; ".join(
              f"{r['variant']}/{r['mode']} {float(r['median_ms_per_tick']):.4f} ms" for r in rows), flush=True)
    print(f"Complete: {len(measurements)} verified configurations in {directory / 'measurements.csv'}", flush=True)


if __name__ == "__main__":
    main()
