#!/usr/bin/env python3
"""Execute DRAMSys HBM2 trace players and summarize the recorded TDB files."""

from __future__ import annotations

import argparse
import csv
import json
import sqlite3
import subprocess
from pathlib import Path


def percentile(values: list[int], percentage: int) -> float:
    ordered = sorted(values)
    return float(ordered[max(0, (percentage * len(ordered) + 99) // 100 - 1)])


def phase_metrics(db: sqlite3.Connection) -> dict[str, int]:
    phase_counts = {name: int(count) for name, count in db.execute(
        "SELECT PhaseName, COUNT(*) FROM Phases GROUP BY PhaseName")}
    command_names = {
        transaction: [name for (name,) in db.execute(
            "SELECT PhaseName FROM Phases WHERE Transact = ? ORDER BY PhaseBegin", (transaction,))]
        for (transaction,) in db.execute("SELECT DISTINCT Transact FROM Phases WHERE Transact IS NOT NULL")
    }
    row_hits = row_misses = row_conflicts = 0
    for phases in command_names.values():
        has_activate = any(name == "ACT" for name in phases)
        if not has_activate:
            row_hits += 1
        elif any(name.startswith("PRE") for name in phases):
            row_conflicts += 1
        else:
            row_misses += 1
    return {
        "act_commands": phase_counts.get("ACT", 0),
        "pre_commands": sum(count for name, count in phase_counts.items() if name.startswith("PRE")),
        "read_commands": sum(count for name, count in phase_counts.items() if name.startswith("RD")),
        "write_commands": sum(count for name, count in phase_counts.items() if name.startswith("WR")),
        "row_hits": row_hits,
        "row_misses": row_misses,
        "row_conflicts": row_conflicts,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--profile", type=Path,
                        default=Path("validation/profiles/hbm2_2000.json"))
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    if profile.get("profile_id") != "hbm2_2000":
        raise ValueError("DRAMSys runner currently supports hbm2_2000 only")
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
        metrics = phase_metrics(db)
        db.close()
        if not records:
            raise RuntimeError(f"DRAMSys recorded no transactions for {trace}")
        latencies = [int(end - generated) for generated, _begin, end, _size in records]
        first_arrival = min(int(generated) for generated, _begin, _end, _size in records)
        last_completion = max(int(end) for _generated, _begin, end, _size in records)
        total_bytes = sum(int(size) for _generated, _begin, _end, size in records)
        rows.append({
            "tool": "dramsys", "profile": profile["profile_id"],
            "trace": trace, "requests": len(records),
            "completed_requests": len(records), "mean_latency_ps": sum(latencies) / len(latencies),
            "p50_latency_ps": percentile(latencies, 50),
            "p95_latency_ps": percentile(latencies, 95),
            "throughput_bytes_per_ns": total_bytes * 1000 / max(1, last_completion - first_arrival),
            **metrics,
            "metric_note": "latency = recorded end minus trace generation time; row locality is phase-derived",
        })
    with (result_dir / "dramsys-summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
