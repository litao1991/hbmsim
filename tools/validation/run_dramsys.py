#!/usr/bin/env python3
"""Execute DRAMSys HBM2 trace players and summarize the recorded TDB files."""

from __future__ import annotations

import argparse
import csv
import sqlite3
import subprocess
from pathlib import Path


def percentile_95(values: list[int]) -> float:
    ordered = sorted(values)
    return float(ordered[max(0, (95 * len(ordered) + 99) // 100 - 1)])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    args = parser.parse_args()
    work_dir = Path("validation/dramsys-runs")
    result_dir = Path("validation/results")
    work_dir.mkdir(parents=True, exist_ok=True)
    result_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for config in sorted(Path("validation/reference-inputs/dramsys").glob("*.json")):
        trace = config.stem
        run_dir = work_dir / trace
        run_dir.mkdir(parents=True, exist_ok=True)
        with (run_dir / "dramsys.log").open("w", encoding="utf-8") as stream:
            subprocess.run([str(args.binary.resolve()), str(config.resolve())], cwd=run_dir,
                           stdout=stream, stderr=subprocess.STDOUT, check=True)
        databases = sorted(run_dir.glob("DRAMSys_*.tdb"))
        if len(databases) != 1:
            raise RuntimeError(f"expected one DRAMSys database for {trace}, found {databases}")
        db = sqlite3.connect(databases[0])
        records = db.execute(
            "SELECT t.TimeOfGeneration, r.begin, r.end, t.DataLength "
            "FROM Transactions AS t JOIN ranges AS r ON t.Range = r.id ORDER BY t.ID").fetchall()
        db.close()
        if not records:
            raise RuntimeError(f"DRAMSys recorded no transactions for {trace}")
        latencies = [int(end - generated) for generated, _begin, end, _size in records]
        first_arrival = min(int(generated) for generated, _begin, _end, _size in records)
        last_completion = max(int(end) for _generated, _begin, end, _size in records)
        total_bytes = sum(int(size) for _generated, _begin, _end, size in records)
        rows.append({
            "tool": "dramsys", "trace": trace, "requests": len(records),
            "completed_requests": len(records), "mean_latency_ps": sum(latencies) / len(latencies),
            "p95_latency_ps": percentile_95(latencies),
            "throughput_bytes_per_ns": total_bytes * 1000 / max(1, last_completion - first_arrival),
            "metric_note": "latency = recorded end minus trace generation time",
        })
    with (result_dir / "dramsys-summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
