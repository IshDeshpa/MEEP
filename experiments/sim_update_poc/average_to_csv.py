import csv
import re
import sys
from pathlib import Path


PATTERN = re.compile(r"Average update_tmr:\s*([0-9]+(?:\.[0-9]+)?)\s*ms")


def main():
    writer = csv.writer(sys.stdout)
    writer.writerow(["program", "average_update_ms"])

    for filename in sys.argv[1:]:
        output = Path(filename).read_text(encoding="utf-8", errors="replace")
        match = PATTERN.search(output)
        if match is None:
            raise SystemExit(f"No average update_tmr found in {filename}")

        writer.writerow([Path(filename).stem, match.group(1)])


if __name__ == "__main__":
    main()
