#!/usr/bin/env python3
"""Gate cross-simulator numeric sanity and workload-level trends."""

from __future__ import annotations

import csv
import math
from pathlib import Path


RESULTS = Path("validation/results")


def read(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return {row["trace"]: row for row in csv.DictReader(stream)}


def number(row: dict[str, str], field: str) -> float:
    value = float(row[field])
    if not math.isfinite(value) or value < 0:
        raise SystemExit(f"reference trend gate: invalid {field}={value}")
    return value


def check(path: Path) -> None:
    rows = read(path)
    for trace in ("row_hit", "row_conflict", "bank_parallel", "write_only"):
        if trace not in rows:
            raise SystemExit(f"reference trend gate: {path} missing {trace}")
        for field in ("mean_latency_ps", "p50_latency_ps", "p95_latency_ps",
                      "throughput_bytes_per_ns"):
            number(rows[trace], field)

    if number(rows["row_conflict"], "mean_latency_ps") <= number(
            rows["row_hit"], "mean_latency_ps"):
        raise SystemExit(f"reference trend gate: {path} conflict is not slower than row hit")
    if number(rows["row_conflict"], "pre_commands") == 0:
        raise SystemExit(f"reference trend gate: {path} conflict emitted no precharge")
    if number(rows["row_hit"], "pre_commands") != 0:
        raise SystemExit(f"reference trend gate: {path} row-hit emitted a precharge")
    if number(rows["write_only"], "read_commands") != 0:
        raise SystemExit(f"reference trend gate: {path} write-only emitted reads")
    if number(rows["write_only"], "write_commands") == 0:
        raise SystemExit(f"reference trend gate: {path} write-only emitted no writes")


def main() -> None:
    paths = (
        RESULTS / "hbmsim-summary.csv",
        RESULTS / "ramulator2-summary.csv",
        RESULTS / "dramsys-summary.csv",
        RESULTS / "hbmsim-hbm3-summary.csv",
        RESULTS / "ramulator2-hbm3-summary.csv",
    )
    for path in paths:
        check(path)
    print("reference trend gate passed: HBM2 x3 and HBM3 x2")


if __name__ == "__main__":
    main()
