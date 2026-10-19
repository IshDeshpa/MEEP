#!/usr/bin/env python3
"""Join verified whole-world timing with a separate Nsight Compute counter pass."""

import argparse
import csv
import io
import json
import math
import re
import shutil
import statistics
import subprocess
from datetime import datetime, timezone
from pathlib import Path


# Each summary is a mean per complete update kernel/tick. Sector hit rates
# below use summed hit/miss counts, rather than averaging tick percentages.
METRICS = {
    "profiled_mean_kernel_ms": ("gpu__time_duration.sum", 1e-6),
    "l1_hit_sectors_per_tick": ("l1tex__t_sectors_lookup_hit.sum", 1),
    "l1_miss_sectors_per_tick": ("l1tex__t_sectors_lookup_miss.sum", 1),
    "l2_hit_sectors_per_tick": ("lts__t_sectors_lookup_hit.sum", 1),
    "l2_miss_sectors_per_tick": ("lts__t_sectors_lookup_miss.sum", 1),
    "dram_read_bytes_per_tick": ("dram__bytes_read.sum", 1),
    "dram_write_bytes_per_tick": ("dram__bytes_write.sum", 1),
    "dram_peak_throughput_percent": ("dram__throughput.avg.pct_of_peak_sustained_elapsed", 1),
    "sm_peak_throughput_percent": ("sm__throughput.avg.pct_of_peak_sustained_elapsed", 1),
    "sm_active_percent": ("sm__cycles_active.avg.pct_of_peak_sustained_elapsed", 1),
    "achieved_occupancy_percent": ("sm__warps_active.avg.pct_of_peak_sustained_active", 1),
    "issue_active_percent": ("smsp__issue_active.avg.pct_of_peak_sustained_active", 1),
    "active_warps_per_scheduler": ("smsp__warps_active.avg.per_cycle_active", 1),
    "eligible_warps_per_scheduler": ("smsp__warps_eligible.avg.per_cycle_active", 1),
    "warp_instructions_per_tick": ("smsp__inst_executed.sum", 1),
    "stall_long_scoreboard_percent": ("smsp__warp_issue_stalled_long_scoreboard_per_warp_active.pct", 1),
    "stall_short_scoreboard_percent": ("smsp__warp_issue_stalled_short_scoreboard_per_warp_active.pct", 1),
    "stall_math_pipe_percent": ("smsp__warp_issue_stalled_math_pipe_throttle_per_warp_active.pct", 1),
    "stall_not_selected_percent": ("smsp__warp_issue_stalled_not_selected_per_warp_active.pct", 1),
}


def write_csv(path, rows):
    if rows:
        temporary = path.with_suffix(path.suffix + ".tmp")
        with temporary.open("w", newline="") as target:
            writer = csv.DictWriter(target, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        temporary.replace(path)


def parse_available_metrics(text):
    lines = text.splitlines()
    header = next((i for i, line in enumerate(lines) if '"metric name"' in line.lower()), None)
    if header is None:
        return set()
    supported = set()
    metadata = {"metric name", "metric type", "metric unit", "metric description"}
    for row in csv.DictReader(io.StringIO("\n".join(lines[header:]))):
        name = next((value for field, value in row.items() if field.lower() == "metric name"), "")
        devices = [value for field, value in row.items() if field.lower() not in metadata]
        if re.fullmatch(r"[A-Za-z0-9_]+__[A-Za-z0-9_.]+", name) and (not devices or "1" in devices):
            supported.add(name)
    return supported


def numeric(value):
    try:
        result = float(value.replace(",", ""))
        return result if math.isfinite(result) else None
    except (ValueError, AttributeError):
        return None


def parse_counters(text):
    """Accept Nsight's long and wide raw CSV formats, with launch IDs intact."""
    lines = text.splitlines()
    header = next((i for i, line in enumerate(lines)
                   if '"ID"' in line and ('"Metric Name"' in line
                       or any(f'"{name}"' in line for name, _ in METRICS.values()))), None)
    if header is None:
        raise RuntimeError("no counter CSV header; inspect the profiler log")
    values, kernels = {}, set()
    requested = {name for name, _ in METRICS.values()}
    for row in csv.DictReader(io.StringIO("\n".join(lines[header:]))):
        launch_id = row.get("ID", "")
        if not launch_id.isdigit():
            continue
        kernel = row.get("Kernel Name", "")
        if kernel:
            kernels.add(kernel)
        records = [(row.get("Metric Name"), row.get("Metric Value"))] if "Metric Name" in row \
            else [(name, row.get(name)) for name in requested if name in row]
        for name, value in records:
            if name not in requested:
                continue
            key = (name, launch_id)
            if key in values:
                raise RuntimeError(f"duplicate counter {name} for launch {launch_id}")
            values[key] = numeric(value)
    return values, sorted(kernels)


def summarize_counters(values, ticks):
    summary, unavailable = {}, []
    for field, (name, scale) in METRICS.items():
        samples = [value for (metric, _), value in values.items() if metric == name]
        if len(samples) != ticks or any(value is None for value in samples):
            summary[field] = ""
            unavailable.append(name)
        else:
            summary[field] = statistics.fmean(samples) * scale
    for cache in ("l1", "l2"):
        hits = summary[f"{cache}_hit_sectors_per_tick"]
        misses = summary[f"{cache}_miss_sectors_per_tick"]
        summary[f"{cache}_sector_hit_percent"] = 100 * hits / (hits + misses) \
            if hits != "" and misses != "" and hits + misses else ""
    return summary, unavailable


def kernel_filter(row):
    kind = {"cube": 0, "face": 1, "edge": 2}[row["variant"]]
    sequential = int(row["mode"] == "sequential")
    return f"regex:.*6updateILN2tv5ShapeE{kind}ELb{sequential}E.*"


def profile_command(ncu, binary, row, ticks, warmup, metrics, directory, case, section_folder=None, verify=True):
    command = [str(ncu)]
    if section_folder:
        command += ["--section-folder", str(section_folder)]
    command += [
        "--kernel-name-base", "mangled", "--kernel-name", kernel_filter(row),
        "--launch-skip", str(warmup), "--launch-count", str(ticks),
        "--metrics", ",".join(metrics), "--replay-mode", "application",
        "--cache-control", "none", "--clock-control", "none",
        "--csv", "--page", "raw", "--print-units", "base",
        "--force-overwrite", "--export", str(directory / f"{case}.ncu-rep"),
        "--log-file", str(directory / f"{case}_raw.csv"),
        str(binary), "--world", row["world_x"], row["world_y"], row["world_z"],
        "--materials", row["materials"], "--variant", row["variant"],
        "--mode", "sequential" if row["mode"] == "sequential" else "independent",
        "--block-size", row["threads_per_block"], "--seed", row["seed"],
        "--preheat-ms", row.get("preheat_ms", "0"),
        "--device", row["device_index"], "--launch", "direct",
        "--instrumented",
        "--ticks", str(ticks), "--warmup", str(warmup), "--repeats", "1",
        "--output", str(directory / f"{case}_instrumented.csv"),
    ]
    if not verify:
        command.append("--no-verify")
    return command


def gpu_memory(device):
    result = subprocess.run([
        "nvidia-smi", f"--id={device}",
        "--query-gpu=name,memory.total,memory.used,memory.free", "--format=csv,noheader,nounits",
    ], check=True, text=True, capture_output=True)
    name, total, used, free = next(csv.reader(result.stdout.splitlines()))
    return {"profile_gpu": name.strip(), "profile_nvml_total_bytes": round(float(total) * 2**20),
            "profile_nvml_used_bytes": round(float(used) * 2**20),
            "profile_nvml_free_bytes": round(float(free) * 2**20)}


def read_measurements(paths):
    rows = []
    for path in paths:
        with path.open(newline="") as source:
            for row in csv.DictReader(source):
                if row.get("verified") != "true" or row.get("scheduler") != "one_per_tile":
                    raise ValueError("profile only verified one-thread-per-tile measurements")
                if row.get("timing_context") != "uninstrumented":
                    raise ValueError("profile only uninstrumented timing CSVs; regenerate older timing files")
                if row.get("variant") not in ("cube", "face", "edge") or row.get("mode") not in \
                        ({"single"} if row.get("variant") == "cube" else {"independent", "sequential"}):
                    raise ValueError("invalid transition variant/mode in timing CSV")
                required = {"device_index", "world_x", "world_y", "world_z", "materials", "tiles",
                            "threads_per_block", "seed", "timed_ticks", "warmup_ticks", "gpu",
                            "required_vram_bytes", "launched_threads", "median_ms_per_tick"}
                if not required.issubset(row):
                    raise ValueError("timing CSV is missing configuration fields; regenerate it with this benchmark")
                row["timing_source_csv"] = str(path.resolve())
                rows.append(row)
    if not rows:
        raise ValueError("no measurements in the supplied timing CSVs")
    return rows


def executable(value):
    resolved = shutil.which(str(value))
    if not resolved:
        raise ValueError(f"executable not found: {value}")
    return Path(resolved).resolve()


def profile_key(row):
    return tuple(row[field] for field in ("gpu", "device_index", "world_x", "world_y", "world_z", "materials",
                                          "variant", "mode", "threads_per_block", "seed", "median_ms_per_tick")) + (row.get("preheat_ms", "0"),)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("measurements", type=Path, nargs="+")
    parser.add_argument("--ncu", default="ncu")
    parser.add_argument("--section-folder", type=Path)
    parser.add_argument("--binary", type=Path, default=Path(__file__).parent / "build/transition_variations")
    parser.add_argument("--ticks", type=int, help="default: timed tick count from each timing row")
    parser.add_argument("--warmup", type=int, help="default: warmup from each timing row")
    parser.add_argument("--headroom-mib", type=int, default=256)
    parser.add_argument("--skip-profile-verify", action="store_true",
                        help="reuse native CPU/GPU verification; skip repeating the CPU reference during replay")
    parser.add_argument("--resume", action="store_true", help="reuse completed matching profiles in the output directory")
    parser.add_argument("--output-dir", type=Path, help="default: counters/ beside the first timing CSV")
    args = parser.parse_args(argv)
    if (args.ticks is not None and args.ticks < 1) or (args.warmup is not None and args.warmup < 0):
        parser.error("ticks must be positive and warmup nonnegative")
    if args.headroom_mib < 0:
        parser.error("headroom must be nonnegative")
    try:
        rows = read_measurements(args.measurements)
        ncu, binary = executable(args.ncu), executable(args.binary)
    except (ValueError, OSError) as error:
        parser.error(str(error))
    directory = (args.output_dir or args.measurements[0].parent / "counters").resolve()
    directory.mkdir(parents=True, exist_ok=True)
    section_folder = args.section_folder.resolve() if args.section_folder else None
    version = subprocess.run([str(ncu), "--version"], capture_output=True, text=True, check=True).stdout.strip()
    available, summaries, failures = {}, [], 0
    previous = {}
    if args.resume and (directory / "report.csv").exists():
        with (directory / "report.csv").open(newline="") as source:
            previous = {profile_key(row): row for row in csv.DictReader(source)}
    empty_counters, _ = summarize_counters({}, 1)
    for index, row in enumerate(rows):
        ticks = args.ticks if args.ticks is not None else int(row["timed_ticks"])
        warmup = args.warmup if args.warmup is not None else int(row["warmup_ticks"])
        saved = previous.get(profile_key(row))
        if saved and saved["profile_status"] == "measured" and int(saved["profiled_ticks"]) == ticks \
                and int(saved["profile_warmup_ticks"]) == warmup and Path(saved["profile_raw_csv"]).exists():
            summaries.append(saved)
            write_csv(directory / "report.csv", summaries)
            print(f"Reusing {row['world_x']} m{row['materials']} {row['variant']} {row['mode']}", flush=True)
            continue
        case = (f"{index:04d}_{row['world_x']}x{row['world_y']}x{row['world_z']}_m{row['materials']}_"
                f"{row['variant']}_{row['mode']}")
        summary = {**row, **empty_counters, "profile_status": "unavailable", "profile_detail": "",
                   "unavailable_metrics": "", "profiled_ticks": ticks, "profile_warmup_ticks": warmup,
                   "profile_launch_mode": "direct", "profile_cache_control": "none",
                   "profile_clock_control": "none", "profile_replay_mode": "application",
                   "profile_verification": "verified_native_source" if args.skip_profile_verify else "cpu_reference",
                   "profile_ncu_version": version, "profile_kernel_filter": kernel_filter(row),
                   "profile_kernel_names": "", "profile_raw_csv": str(directory / f"{case}_raw.csv"),
                   "profile_report": str(directory / f"{case}.ncu-rep"),
                   "profile_checked_at_utc": datetime.now(timezone.utc).isoformat(),
                   "profile_estimated_required_bytes": "", "profile_headroom_bytes": args.headroom_mib * 2**20,
                   "profile_gpu": "", "profile_nvml_total_bytes": "", "profile_nvml_used_bytes": "",
                   "profile_nvml_free_bytes": "", "profile_cuda_free_bytes": "", "profile_cuda_used_bytes": ""}
        try:
            cells, entry_bytes = {"cube": (8, 4), "face": (4, 2), "edge": (2, 1)}[row["variant"]]
            required = int(row["materials"])**cells * entry_bytes + int(row["tiles"]) * 8
            if required != int(row["required_vram_bytes"]):
                raise ValueError("timing CSV and recomputed memory estimate disagree")
            summary["profile_estimated_required_bytes"] = required
            summary.update(gpu_memory(row["device_index"]))
            if summary["profile_gpu"] != row["gpu"]:
                raise RuntimeError("current GPU differs from the timing CSV's GPU")
            if required + summary["profile_headroom_bytes"] > summary["profile_nvml_free_bytes"]:
                raise RuntimeError("insufficient current VRAM including profiler headroom")
            device = row["device_index"]
            if device not in available:
                bases = sorted({name.split(".", 1)[0] for name, _ in METRICS.values()})
                query = subprocess.run([str(ncu), "--query-metrics-mode", "suffix", "--metrics", ",".join(bases),
                                        "--devices", device, "--csv"], capture_output=True, text=True, check=True)
                (directory / f"available_metrics_device{device}.csv").write_text(query.stdout)
                available[device] = parse_available_metrics(query.stdout)
            supported = [name for name, _ in METRICS.values() if name in available[device]]
            if not supported:
                raise RuntimeError("none of the requested hardware counters are available")
            command = profile_command(ncu, binary, row, ticks, warmup, supported, directory, case, section_folder,
                                      verify=not args.skip_profile_verify)
            (directory / f"{case}_command.json").write_text(json.dumps(command, indent=2) + "\n")
            print(f"Profiling {case}: {len(supported)} counters, {ticks} whole-world ticks", flush=True)
            with (directory / f"{case}.log").open("w") as log:
                result = subprocess.run(command, stdout=log, stderr=log)
            if result.returncode:
                raise RuntimeError(f"ncu exited {result.returncode}; see {case}.log and raw CSV")
            with (directory / f"{case}_instrumented.csv").open(newline="") as source:
                instrumented = list(csv.DictReader(source))
            expected_verification = "false" if args.skip_profile_verify else "true"
            if len(instrumented) != 1 or instrumented[0]["verified"] != expected_verification \
                    or instrumented[0]["timing_context"] != "instrumented":
                raise RuntimeError("instrumented benchmark did not verify exactly one variant")
            for field in ("variant", "mode", "tiles", "launched_threads", "threads_per_block", "seed"):
                if instrumented[0][field] != row[field]:
                    raise RuntimeError(f"instrumented benchmark changed {field}")
            summary["profile_cuda_free_bytes"] = instrumented[0]["cuda_free_vram_bytes_before_alloc"]
            summary["profile_cuda_used_bytes"] = instrumented[0]["cuda_used_vram_bytes_before_alloc"]
            values, kernels = parse_counters(Path(summary["profile_raw_csv"]).read_text())
            if len(kernels) != 1:
                raise RuntimeError("expected counter records for exactly one update kernel")
            counters, unavailable = summarize_counters(values, ticks)
            summary.update(counters)
            summary["unavailable_metrics"] = ";".join(unavailable)
            summary["profile_kernel_names"] = ";".join(kernels)
            if not any(summary[field] != "" for field in METRICS):
                raise RuntimeError("no complete counter records for the requested tick count")
            summary["profile_status"] = "partial" if unavailable else "measured"
            if unavailable:
                print(f"  Unavailable counters: {'; '.join(unavailable)}", flush=True)
        except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
            summary["profile_detail"] = str(error)
            summary["unavailable_metrics"] = ";".join(name for name, _ in METRICS.values())
            failures += 1
            print(f"  Counters unavailable: {error}", flush=True)
        summaries.append(summary)
        write_csv(directory / "report.csv", summaries)
    print(f"Saved uninstrumented tick times + counters: {directory / 'report.csv'}", flush=True)
    return 2 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
