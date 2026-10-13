#!/usr/bin/env python3
"""Run X×X×100 worlds at a chosen material count, checking VRAM before each case."""

import argparse
import csv
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def write_csv(path, rows, fields=None):
    if not rows:
        return
    with path.open("w", newline="") as target:
        writer = csv.DictWriter(target, fieldnames=fields or list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def gpu_memory(device):
    result = subprocess.run(
        ["nvidia-smi", f"--id={device}",
         "--query-gpu=name,memory.total,memory.used,memory.free",
         "--format=csv,noheader,nounits"],
        check=True, capture_output=True, text=True,
    )
    name, total, used, free = next(csv.reader(result.stdout.splitlines()))
    return {
        "gpu": name.strip(),
        "nvml_total_bytes": round(float(total) * 2**20),
        "nvml_used_bytes": round(float(used) * 2**20),
        "nvml_free_bytes": round(float(free) * 2**20),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/thread_count"))
    parser.add_argument("--sizes", type=int, nargs="+", default=[100, 200, 300, 400, 500])
    parser.add_argument("--height", type=int, default=100)
    parser.add_argument("--materials", type=int, choices=range(2, 17), default=8)
    parser.add_argument("--ticks", type=int, default=10)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--block-size", type=int, default=256)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--headroom-mib", type=int, default=256)
    parser.add_argument("--workers", help="optional comma-separated workers; default automatic sweep")
    parser.add_argument("--min-workers", type=int, default=1,
                        help="lower bound for automatic sweep; use 32 to skip very slow serial points")
    parser.add_argument("--output-dir", type=Path, help="default: results/world_sizes_<materials>")
    args = parser.parse_args()
    if any(value <= 0 or value % 2 for value in [*args.sizes, args.height]):
        parser.error("world dimensions must be positive and even")
    if min(args.ticks, args.repeats, args.block_size, args.min_workers) < 1 or args.warmup < 0:
        parser.error("invalid tick/repeat/block/warmup values")
    if args.headroom_mib < 0 or args.device < 0:
        parser.error("headroom and device must be nonnegative")
    binary = args.binary.resolve()
    if not binary.is_file():
        parser.error(f"benchmark binary not found: {binary}")
    if args.output_dir is None:
        args.output_dir = Path(f"results/world_sizes_{args.materials}")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    memory_rows, measurements = [], []
    failures = 0
    for size in args.sizes:
        # Four-byte table entries and two four-byte packed tile buffers.
        tiles = (size // 2) ** 2 * (args.height // 2)
        lut_bytes, world_buffers_bytes = args.materials**8 * 4, tiles * 4 * 2
        required = lut_bytes + world_buffers_bytes
        case = f"{size}x{size}x{args.height}"
        output = args.output_dir / f"{case}.csv"
        memory = {
            "checked_at_utc": datetime.now(timezone.utc).isoformat(),
            "world_x": size, "world_y": size, "world_z": args.height,
            "materials": args.materials, "tiles": tiles, "lut_bytes": lut_bytes,
            "world_buffers_bytes": world_buffers_bytes, "estimated_required_bytes": required,
            "headroom_bytes": args.headroom_mib * 2**20,
            **gpu_memory(args.device),
            "cuda_total_vram_bytes_before_alloc": "",
            "cuda_free_vram_bytes_before_alloc": "",
            "cuda_used_vram_bytes_before_alloc": "",
            "status": "", "measurement_csv": output.name, "detail": "",
        }
        print(
            f"{case}, {args.materials} materials: need {required / 2**20:.2f} MiB + "
            f"{args.headroom_mib} MiB headroom; "
            f"currently free {memory['nvml_free_bytes'] / 2**20:.0f} MiB, "
            f"used {memory['nvml_used_bytes'] / 2**20:.0f} MiB", flush=True,
        )
        if required + memory["headroom_bytes"] > memory["nvml_free_bytes"]:
            memory["status"] = "insufficient_vram"
            memory["detail"] = "estimate plus headroom exceeds currently free VRAM"
            failures += 1
        else:
            command = [
                str(binary), "--world", str(size), str(size), str(args.height),
                "--materials", str(args.materials), "--access", "lookup", "--launch", "graph",
                "--min-workers", str(args.min_workers),
                "--ticks", str(args.ticks), "--warmup", str(args.warmup),
                "--repeats", str(args.repeats), "--block-sizes", str(args.block_size),
                "--device", str(args.device), "--output", str(output),
            ]
            if args.workers:
                command.extend(["--workers", args.workers])
            with (args.output_dir / f"{case}.log").open("w") as log:
                completed = subprocess.run(command, stdout=log, stderr=log)
            if completed.returncode:
                memory["status"] = "failed"
                memory["detail"] = f"benchmark exited {completed.returncode}; see {case}.log"
                failures += 1
            else:
                with output.open(newline="") as source:
                    rows = list(csv.DictReader(source))
                if not rows or any(row["verified"] != "true" or row["access"] != "lookup" for row in rows):
                    raise RuntimeError(f"unverified or unexpected results for {case}")
                if any(int(row["required_vram_bytes"]) != required for row in rows):
                    raise RuntimeError(f"CUDA and host memory estimates disagree for {case}")
                measurements.extend(rows)
                for field in (
                    "cuda_total_vram_bytes_before_alloc", "cuda_free_vram_bytes_before_alloc",
                    "cuda_used_vram_bytes_before_alloc",
                ):
                    memory[field] = rows[0][field]
                memory["status"] = "passed"
                fastest = min(rows, key=lambda row: float(row["median_ms_per_tick"]))
                print(
                    f"  Passed {len(rows)} configurations. Fastest median "
                    f"{float(fastest['median_ms_per_tick']):.6f} ms/tick, "
                    f"{fastest['launched_threads']} threads ({fastest['scheduler']}).", flush=True,
                )
        memory_rows.append(memory)
        write_csv(args.output_dir / "memory_checks.csv", memory_rows)
        write_csv(args.output_dir / "measurements.csv", measurements)
    print(f"Saved memory checks and measurements in {args.output_dir}")
    return int(failures > 0)


if __name__ == "__main__":
    raise SystemExit(main())
