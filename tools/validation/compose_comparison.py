#!/usr/bin/env python3
"""Create one auditable comparison table from the three tool summaries."""

from __future__ import annotations

import csv
from pathlib import Path


RESULTS = Path("validation/results")


def main() -> None:
    rows = []
    for filename in ("hbmsim-summary.csv", "ramulator2-summary.csv", "dramsys-summary.csv"):
        with (RESULTS / filename).open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream):
                row.setdefault("completed_requests", row.get("requests", ""))
                row.setdefault("metric_note", "")
                row["comparison_scope"] = "same operation/address sequence; HBM2 timing/topology not fully harmonized"
                rows.append(row)
    fields = ["tool", "trace", "requests", "completed_requests", "mean_latency_ps",
              "p95_latency_ps", "throughput_bytes_per_ns", "comparison_scope", "metric_note"]
    with (RESULTS / "three-simulator-comparison.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
