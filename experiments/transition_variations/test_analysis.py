"""Check comparison units, missing data, and resume configuration matching."""

import csv
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import analyze
import sweep


def timing_row():
    return {"world_x": "4", "world_y": "2", "world_z": "4", "tiles": "4", "materials": "8",
            "variant": "cube", "mode": "single", "median_ms_per_tick": "2", "verified": "true",
            "timing_context": "uninstrumented", "scheduler": "one_per_tile", "launch_mode": "graph",
            "threads_per_block": "256", "seed": "42", "warmup_ticks": "2", "timed_ticks": "10",
            "repeats": "5", "device_index": "0", "preheat_ms": "500"}


class ComparisonChecks(unittest.TestCase):
    def test_normalization_is_per_world_tile(self):
        row = timing_row()
        row.update(dram_read_bytes_per_tick="100", dram_write_bytes_per_tick="20",
                   warp_instructions_per_tick="12", l2_hit_sectors_per_tick="8", l2_miss_sectors_per_tick="4")
        result = analyze.enrich(row)
        self.assertEqual(result["dram_read_bytes_per_tile"], 25)
        self.assertEqual(result["dram_total_bytes_per_tile"], 30)
        self.assertEqual(result["warp_instructions_per_tile"], 3)
        self.assertEqual(result["l2_total_sectors_per_tile"], 3)
        self.assertEqual(result["aggregate_ns_per_tile"], 500000)

    def test_missing_counters_stay_missing_and_zero_stays_zero(self):
        row = timing_row()
        row.update(dram_read_bytes_per_tick="", dram_write_bytes_per_tick="0")
        result = analyze.enrich(row)
        self.assertEqual(result["dram_read_bytes_per_tile"], "")
        self.assertEqual(result["dram_write_bytes_per_tile"], 0)
        self.assertEqual(result["dram_total_bytes_per_tile"], "")

    def test_geometry_and_duplicate_points_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.csv"
            row = timing_row()
            for rows in [[{**row, "tiles": "8"}], [row, row]]:
                with path.open("w", newline="") as target:
                    writer = csv.DictWriter(target, fieldnames=list(row))
                    writer.writeheader()
                    writer.writerows(rows)
                with self.assertRaises(ValueError):
                    analyze.load_rows(path)

    def test_resume_requires_all_five_matching_variants_and_preheat(self):
        rows = [{**timing_row(), "variant": variant, "mode": mode} for variant, mode in analyze.CONFIGS]
        args = SimpleNamespace(seed=42, block_size=256, warmup=2, ticks=10, repeats=5, device=0, preheat_ms=500)
        self.assertTrue(sweep.expected_case(rows, 4, 2, 8, args))
        self.assertFalse(sweep.expected_case(rows[:-1], 4, 2, 8, args))
        rows[0]["preheat_ms"] = "0"
        self.assertFalse(sweep.expected_case(rows, 4, 2, 8, args))


if __name__ == "__main__":
    unittest.main()
