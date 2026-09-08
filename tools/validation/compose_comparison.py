#!/usr/bin/env python3
"""Create one auditable comparison table from the three tool summaries."""

from __future__ import annotations

import csv
import argparse
import json
from pathlib import Path


RESULTS = Path("validation/results")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("hbm2_2000", "hbm3_6400"),
                        default="hbm2_2000")
    args = parser.parse_args()
    hbm3 = args.profile == "hbm3_6400"
    metadata_name = "hbm3-metadata.json" if hbm3 else "metadata.json"
    metadata = json.loads((Path("validation/reference-inputs") / metadata_name).read_text(
        encoding="utf-8"))
    rows = []
    filenames = (("hbmsim-hbm3-summary.csv", "ramulator2-hbm3-summary.csv") if hbm3 else
                 ("hbmsim-summary.csv", "ramulator2-summary.csv", "dramsys-summary.csv"))
    for filename in filenames:
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
    output = "hbm3-two-simulator-comparison.csv" if hbm3 else "three-simulator-comparison.csv"
    with (RESULTS / output).open("w", newline="", encoding="utf-8") as stream:
        # HBMSim also publishes implementation-specific resource and latency
        # stage columns. Keep the cross-tool table restricted to the declared
        # common schema while retaining those details in its source summary.
        writer = csv.DictWriter(stream, fieldnames=fields,
                                extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
