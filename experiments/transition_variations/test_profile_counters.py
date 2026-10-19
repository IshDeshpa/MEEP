"""Portable checks for hardware-counter parsing and report integrity."""

import csv
import io
import re
import tempfile
import unittest
from pathlib import Path

import profile_counters as profile


class CounterChecks(unittest.TestCase):
    def long_csv(self, records):
        stream = io.StringIO()
        stream.write("==PROF== Connected\n")
        writer = csv.writer(stream, quoting=csv.QUOTE_ALL)
        writer.writerow(["ID", "Kernel Name", "Metric Name", "Metric Unit", "Metric Value"])
        for launch, name, value in records:
            writer.writerow([launch, "update<face, independent>", name, "unit", value])
        return stream.getvalue()

    def test_weighted_hits_and_separate_instrumented_duration(self):
        metrics = dict((field, name) for field, (name, _) in profile.METRICS.items())
        records = []
        for launch, hits, misses, ns in [(0, 1, 9, "1,000,000"), (1, 90, 10, "3,000,000")]:
            records.extend([(launch, metrics["l1_hit_sectors_per_tick"], hits),
                            (launch, metrics["l1_miss_sectors_per_tick"], misses),
                            (launch, metrics["profiled_mean_kernel_ms"], ns)])
        values, kernels = profile.parse_counters(self.long_csv(records))
        result, unavailable = profile.summarize_counters(values, 2)
        self.assertAlmostEqual(result["l1_sector_hit_percent"], 100 * 91 / 110)
        self.assertEqual(result["profiled_mean_kernel_ms"], 2)
        self.assertEqual(result["dram_read_bytes_per_tick"], "")
        self.assertIn(metrics["dram_read_bytes_per_tick"], unavailable)
        self.assertEqual(kernels, ["update<face, independent>"])

    def test_missing_ticks_and_unavailable_values_stay_blank(self):
        metric = profile.METRICS["dram_read_bytes_per_tick"][0]
        values, _ = profile.parse_counters(self.long_csv([(0, metric, 64)]))
        self.assertEqual(profile.summarize_counters(values, 2)[0]["dram_read_bytes_per_tick"], "")
        for value in ["N/A", "nan", "inf", ""]:
            values, _ = profile.parse_counters(self.long_csv([(0, metric, value)]))
            self.assertEqual(profile.summarize_counters(values, 1)[0]["dram_read_bytes_per_tick"], "")

    def test_wide_format_and_zero_sector_denominator(self):
        stream = io.StringIO()
        writer = csv.writer(stream, quoting=csv.QUOTE_ALL)
        hit, miss = [profile.METRICS[field][0] for field in ("l1_hit_sectors_per_tick", "l1_miss_sectors_per_tick")]
        writer.writerow(["ID", "Kernel Name", hit, miss])
        writer.writerow(["", "", "sector", "sector"])
        writer.writerow([0, "update", 0, 0])
        values, _ = profile.parse_counters(stream.getvalue())
        self.assertEqual(profile.summarize_counters(values, 1)[0]["l1_sector_hit_percent"], "")

    def test_duplicate_metric_records_are_rejected(self):
        metric = profile.METRICS["dram_read_bytes_per_tick"][0]
        with self.assertRaises(RuntimeError):
            profile.parse_counters(self.long_csv([(0, metric, 64), (0, metric, 64)]))

    def test_profile_reproduces_seed_block_and_variant(self):
        row = {"variant": "edge", "mode": "sequential", "world_x": "10", "world_y": "14",
               "world_z": "18", "materials": "14", "threads_per_block": "256",
               "seed": "8675309", "device_index": "0"}
        command = profile.profile_command(Path("/ncu"), Path("/benchmark"), row, 3, 2,
                                          ["counter.sum"], Path("/results"), "case")
        for flag, expected in [("--seed", "8675309"), ("--block-size", "256"),
                               ("--variant", "edge"), ("--mode", "sequential"),
                               ("--launch-skip", "2"), ("--launch-count", "3"),
                               ("--replay-mode", "application"), ("--cache-control", "none")]:
            self.assertEqual(command[command.index(flag) + 1], expected)
        self.assertNotIn("--workers", command)
        self.assertIn("--instrumented", command)
        self.assertIn("ShapeE2ELb1E", profile.kernel_filter(row))
        pattern = profile.kernel_filter(row).removeprefix("regex:")
        self.assertRegex("_ZN99example6updateILN2tv5ShapeE2ELb1EEEv", pattern)
        self.assertIsNone(re.search(pattern, "_ZN99example11heat_updateILN2tv5ShapeE2ELb1EEEv"))
        replay = profile.profile_command(Path("/ncu"), Path("/benchmark"), row, 5, 2,
                                         ["counter.sum"], Path("/results"), "case", verify=False)
        self.assertIn("--no-verify", replay)

    def test_unsupported_counters_are_not_invented(self):
        supported = profile.parse_available_metrics('"Metric Name"\n"dram__bytes_read.sum"\n')
        self.assertEqual(supported, {"dram__bytes_read.sum"})
        result, unavailable = profile.summarize_counters({}, 1)
        self.assertTrue(all(value == "" for value in result.values()))
        self.assertEqual(len(unavailable), len(profile.METRICS))
        self.assertEqual(profile.parse_available_metrics(
            '"Metric name","Metric type","Metric unit","Metric description","GPU"\n'
            '"dram__bytes_read.sum","Counter","byte","Reads","0"\n'), set())

    def test_unverified_measurements_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "timing.csv"
            source.write_text("verified,scheduler\nfalse,one_per_tile\n")
            with self.assertRaises(ValueError):
                profile.read_measurements([source])

    def test_instrumented_measurements_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "timing.csv"
            source.write_text("verified,scheduler,timing_context\ntrue,one_per_tile,instrumented\n")
            with self.assertRaises(ValueError):
                profile.read_measurements([source])


if __name__ == "__main__":
    unittest.main()
