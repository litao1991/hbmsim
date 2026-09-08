#!/usr/bin/env python3
"""Reject unintended HBMSim HBM2 numerical drift from the v0.6.4 baseline."""

from __future__ import annotations

import csv
import math
from pathlib import Path


BASELINE = Path("validation/baselines/hbmsim-hbm2_2000-v0.6.4.csv")
CURRENT = Path("validation/results/hbmsim-summary.csv")
EXACT_FIELDS = ("requests", "act_commands", "pre_commands", "read_commands",
                "write_commands", "row_hits", "row_misses", "row_conflicts")
FLOAT_FIELDS = ("mean_latency_ps", "p50_latency_ps", "p95_latency_ps",
                "throughput_bytes_per_ns")


def keyed(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return {row["trace"]: row for row in csv.DictReader(stream)}


def main() -> None:
    baseline = keyed(BASELINE)
    current = keyed(CURRENT)
    if set(baseline) != set(current):
        raise SystemExit("HBM2 numerical gate: trace set changed")
    for trace in baseline:
        for field in EXACT_FIELDS:
            if int(current[trace][field]) != int(baseline[trace][field]):
                raise SystemExit(f"HBM2 numerical gate: {trace}/{field} drifted")
        for field in FLOAT_FIELDS:
            if not math.isclose(float(current[trace][field]), float(baseline[trace][field]),
                                rel_tol=1e-6, abs_tol=1e-6):
                raise SystemExit(f"HBM2 numerical gate: {trace}/{field} drifted")
    print(f"HBM2 numerical gate passed: {len(current)} traces match frozen v0.6.4")


if __name__ == "__main__":
    main()
