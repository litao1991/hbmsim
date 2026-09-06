#!/usr/bin/env python3
"""Run HBMSim micro-traces and emit a normalized result table."""

import csv
import math
import subprocess
import sys
from pathlib import Path


def percentile_95(values: list[int]) -> float:
    ordered = sorted(values)
    index = max(0, math.ceil(0.95 * len(ordered)) - 1)
    return float(ordered[index])


def main() -> None:
    binary = Path(sys.argv[1])
    trace_dir = Path("validation/traces")
    result_dir = Path("validation/results")
    result_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for trace in sorted(trace_dir.glob("*.csv")):
        completion_file = result_dir / f"hbmsim-{trace.stem}-completions.csv"
        subprocess.run([str(binary), str(trace), "--completions", str(completion_file)], check=True,
                       stdout=subprocess.DEVNULL)
        with completion_file.open(newline="", encoding="utf-8") as stream:
            completions = list(csv.DictReader(stream))
        latencies = [int(item["latency_ps"]) for item in completions]
        first_arrival = min(int(item["arrival_ps"]) for item in completions)
        last_completion = max(int(item["completion_ps"]) for item in completions)
        total_bytes = sum(int(item["size_bytes"]) for item in completions)
        duration = max(1, last_completion - first_arrival)
        rows.append({
            "tool": "hbmsim", "trace": trace.stem,
            "requests": len(completions), "mean_latency_ps": sum(latencies) / len(latencies),
            "p95_latency_ps": percentile_95(latencies),
            "throughput_bytes_per_ns": total_bytes * 1000 / duration,
        })
    with (result_dir / "hbmsim-summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
