#!/usr/bin/env python3
import re
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: parse_coverage_summary.py <report_path> <min_line_coverage>", file=sys.stderr)
        return 2

    report_path = Path(sys.argv[1])
    minimum = float(sys.argv[2])

    text = report_path.read_text(encoding="utf-8")
    total_line = None
    for line in text.splitlines():
        if line.startswith("TOTAL"):
            total_line = line
            break

    if total_line is None:
        print("error: TOTAL row not found in coverage report", file=sys.stderr)
        return 2

    percents = re.findall(r"([0-9]+(?:\.[0-9]+)?)%", total_line)
    if len(percents) < 2:
        print("error: expected at least two coverage percentages in TOTAL row", file=sys.stderr)
        return 2

    # TOTAL row order: Regions, Functions, Lines, Branches. We gate by line coverage.
    line_coverage = float(percents[2 if len(percents) > 2 else 1])
    print(f"TOTAL_LINE_COVERAGE={line_coverage:.2f}")

    if line_coverage + 1e-9 < minimum:
        print(f"error: coverage {line_coverage:.2f}% is below threshold {minimum:.2f}%", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
