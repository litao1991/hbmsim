#!/usr/bin/env python3
"""Run HBMSim micro-traces and emit a normalized result table."""

import csv
import argparse
import math
import subprocess
from pathlib import Path


def percentile(values: list[int], percentage: int) -> float:
    ordered = sorted(values)
    index = max(0, math.ceil(percentage / 100 * len(ordered)) - 1)
    return float(ordered[index])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--profile", choices=("hbm2_2000", "hbm3_6400"), required=True)
    args = parser.parse_args()
    binary = args.binary
    trace_dir = Path("validation/traces") / args.profile
    result_dir = Path("validation/results")
    result_dir.mkdir(parents=True, exist_ok=True)
    suffix = "" if args.profile == "hbm2_2000" else "-hbm3"
    rows = []
    for trace in sorted(trace_dir.glob("*.csv")):
        completion_file = result_dir / f"hbmsim{suffix}-{trace.stem}-completions.csv"
        process = subprocess.run(
            [str(binary), str(trace), "--profile", args.profile,
             "--completions", str(completion_file)],
            check=True, stdout=subprocess.PIPE, text=True)
        metrics = dict(line.split(",", 1) for line in process.stdout.splitlines() if "," in line)
        with completion_file.open(newline="", encoding="utf-8") as stream:
            completions = list(csv.DictReader(stream))
        latencies = [int(item["latency_ps"]) for item in completions]
        stage_names = ("queue_wait_ps", "command_phase_ps", "data_ready_ps",
                       "data_bus_wait_ps", "data_service_ps")
        for item in completions:
            stage_total = sum(int(item[name]) for name in stage_names)
            if stage_total != int(item["latency_ps"]):
                raise RuntimeError(
                    f"{trace}: completion {item['id']} stage total {stage_total} "
                    f"!= latency {item['latency_ps']}")
        first_arrival = min(int(item["arrival_ps"]) for item in completions)
        last_completion = max(int(item["completion_ps"]) for item in completions)
        total_bytes = sum(int(item["size_bytes"]) for item in completions)
        duration = max(1, last_completion - first_arrival)
        rows.append({
            "tool": "hbmsim", "trace": trace.stem,
            "profile": args.profile,
            "requests": len(completions), "mean_latency_ps": sum(latencies) / len(latencies),
            "p50_latency_ps": percentile(latencies, 50),
            "p95_latency_ps": percentile(latencies, 95),
            "throughput_bytes_per_ns": total_bytes * 1000 / duration,
            "act_commands": metrics["act_commands"], "pre_commands": metrics["pre_commands"],
            "read_commands": metrics["read_commands"], "write_commands": metrics["write_commands"],
            "row_hits": metrics["row_hits"], "row_misses": metrics["row_closed"],
            "row_conflicts": metrics["row_conflicts"],
            "row_command_bus_busy_ps": metrics["channel_0_row_command_bus_busy_ps"],
            "column_command_bus_busy_ps": metrics["channel_0_column_command_bus_busy_ps"],
            "queue_wait_ps": metrics["channel_0_queue_wait_ps"],
            "command_phase_ps": metrics["channel_0_command_wait_ps"],
            "data_ready_ps": metrics["channel_0_array_wait_ps"],
            "data_bus_wait_ps": metrics["channel_0_data_bus_wait_ps"],
            "data_service_ps": metrics["channel_0_data_service_ps"],
            "merged_accesses": metrics["channel_0_merged_accesses"],
        })
    with (result_dir / f"hbmsim{suffix}-summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
