#!/usr/bin/env python3

import argparse
import csv
import re
import subprocess
from pathlib import Path


GRID_PATTERN = re.compile(r"constexpr int GRID_SZ = \d+;")


def parse_grid_sizes(arguments):
    sizes = []
    for argument in arguments:
        sizes.extend(argument.split())

    try:
        sizes = [int(size) for size in sizes]
    except ValueError as error:
        raise SystemExit(f"Grid sizes must be integers: {error}") from error

    if not sizes or any(size <= 0 for size in sizes):
        raise SystemExit("Grid sizes must be positive integers")

    return sizes


def read_averages(path):
    with path.open(newline="") as csv_file:
        return list(csv.DictReader(csv_file))


def main():
    parser = argparse.ArgumentParser(description="Sweep Game of Life grid sizes")
    parser.add_argument(
        "grid_sizes",
        nargs="+",
        help="space-separated grid sizes, quoted or unquoted",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="sweep_averages.csv",
        help="output CSV path",
    )
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent
    config_path = repo / "gol_config.h"
    averages_path = repo / "averages.csv"
    original_config = config_path.read_text()
    grid_sizes = parse_grid_sizes(args.grid_sizes)
    results = []

    try:
        if GRID_PATTERN.search(original_config) is None:
            raise SystemExit("Could not find GRID_SZ in gol_config.h")

        for grid_size in grid_sizes:
            updated_config = GRID_PATTERN.sub(
                f"constexpr int GRID_SZ = {grid_size};",
                original_config,
                count=1,
            )
            config_path.write_text(updated_config)

            log_path = repo / f"sweep_{grid_size}.log"
            print(f"Running GRID_SZ={grid_size}...")
            with log_path.open("w") as log_file:
                subprocess.run(
                    ["make", "all"],
                    cwd=repo,
                    stdout=log_file,
                    stderr=subprocess.STDOUT,
                    check=True,
                )

            for row in read_averages(averages_path):
                results.append({
                    "grid_size": grid_size,
                    "program": row["program"],
                    "average_update_ms": row["average_update_ms"],
                })
    finally:
        config_path.write_text(original_config)

    output_path = repo / args.output
    with output_path.open("w", newline="") as csv_file:
        writer = csv.DictWriter(
            csv_file,
            fieldnames=["grid_size", "program", "average_update_ms"],
        )
        writer.writeheader()
        writer.writerows(results)

    print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()
