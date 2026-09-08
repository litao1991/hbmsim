#!/usr/bin/env python3
"""Validate workload semantics and exclusive HBMSim latency stages."""

from __future__ import annotations

import csv
from pathlib import Path


RESULTS = Path("validation/results")
STAGES = ("queue_wait_ps", "command_phase_ps", "data_ready_ps",
          "data_bus_wait_ps", "data_service_ps")


def read(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def integer(row: dict[str, str], name: str) -> int:
    return int(row[name])


def check_profile(suffix: str) -> None:
    summary_path = RESULTS / f"hbmsim{suffix}-summary.csv"
    rows = {row["trace"]: row for row in read(summary_path)}
    required = {"row_hit", "row_conflict", "bank_parallel", "read_write_mix",
                "sequential", "write_only", "queue_pressure"}
    if set(rows) != required:
        raise SystemExit(f"semantic gate: unexpected trace set in {summary_path}")

    row_hit = rows["row_hit"]
    if integer(row_hit, "act_commands") != 1 or integer(row_hit, "pre_commands") != 0:
        raise SystemExit(f"semantic gate: {suffix or 'hbm2'} row_hit lost one-row behavior")
    conflict = rows["row_conflict"]
    if integer(conflict, "act_commands") <= 1 or integer(conflict, "pre_commands") == 0:
        raise SystemExit(f"semantic gate: {suffix or 'hbm2'} row_conflict has no row turnover")
    parallel = rows["bank_parallel"]
    if integer(parallel, "act_commands") != 16 or integer(parallel, "pre_commands") != 0:
        raise SystemExit(f"semantic gate: {suffix or 'hbm2'} bank_parallel mapping changed")
    write_only = rows["write_only"]
    if integer(write_only, "read_commands") != 0 or integer(write_only, "write_commands") == 0:
        raise SystemExit(f"semantic gate: {suffix or 'hbm2'} write_only issued the wrong data command")

    for trace, row in rows.items():
        completions = read(RESULTS / f"hbmsim{suffix}-{trace}-completions.csv")
        if len(completions) != integer(row, "requests"):
            raise SystemExit(f"semantic gate: {suffix}/{trace} completion count mismatch")
        for completion in completions:
            stage_total = sum(integer(completion, name) for name in STAGES)
            if stage_total != integer(completion, "latency_ps"):
                raise SystemExit(
                    f"semantic gate: {suffix}/{trace}/{completion['id']} stages do not partition latency")
        data_commands = integer(row, "read_commands") + integer(row, "write_commands")
        merged = integer(row, "merged_accesses")
        if data_commands + merged != integer(row, "requests"):
            raise SystemExit(
                f"semantic gate: {suffix}/{trace} physical commands + merges != logical requests")


def main() -> None:
    check_profile("")
    check_profile("-hbm3")
    print("semantic gate passed: HBM2/HBM3 mapping, commands, merging, and latency stages")


if __name__ == "__main__":
    main()
