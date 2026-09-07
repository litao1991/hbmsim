#!/usr/bin/env python3
"""Create one auditable comparison table from the three tool summaries."""

from __future__ import annotations

import csv
import json
from pathlib import Path


RESULTS = Path("validation/results")


def main() -> None:
    metadata = json.loads((Path("validation/reference-inputs") / "metadata.json").read_text(
        encoding="utf-8"))
    rows = []
    for filename in ("hbmsim-summary.csv", "ramulator2-summary.csv", "dramsys-summary.csv"):
        with (RESULTS / filename).open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream):
                row.setdefault("completed_requests", row.get("requests", ""))
                row.setdefault("metric_note", "")
                row["profile"] = metadata["profile_id"]
                row["comparison_scope"] = metadata["comparison_status"]
                rows.append(row)
    fields = ["tool", "profile", "trace", "requests", "completed_requests", "mean_latency_ps", "p50_latency_ps",
              "p95_latency_ps", "throughput_bytes_per_ns", "act_commands", "pre_commands", "read_commands",
              "write_commands", "row_hits", "row_misses", "row_conflicts", "comparison_scope", "metric_note"]
    with (RESULTS / "three-simulator-comparison.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
