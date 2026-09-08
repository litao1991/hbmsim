#!/usr/bin/env python3
"""Fail CI when a three-simulator H6 run is incomplete or misaligned."""

from __future__ import annotations

import csv
from pathlib import Path


RESULTS = Path("validation/results")
TRACES = Path("validation/traces")
TOOLS = ("hbmsim", "ramulator2", "dramsys")


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def assert_tool_set(expected: dict[str, int], tools: tuple[str, ...], suffix: str = "") -> None:
    for tool in tools:
        summary_path = RESULTS / f"{tool}{suffix}-summary.csv"
        if not summary_path.exists():
            raise SystemExit(f"H6 gate: missing {summary_path}")
        rows = {row["trace"]: row for row in read_rows(summary_path)}
        if set(rows) != set(expected):
            raise SystemExit(
                f"H6 gate: {tool}{suffix} trace set {sorted(rows)} does not match {sorted(expected)}")
        for trace, request_count in expected.items():
            row = rows[trace]
            submitted = int(row["requests"])
            completed = int(row.get("completed_requests", submitted))
            if submitted != request_count or completed != request_count:
                raise SystemExit(
                    f"H6 gate: {tool}{suffix}/{trace} submitted={submitted}, completed={completed}, "
                    f"expected={request_count}")


def main() -> None:
    expected = {
        trace.stem: len(read_rows(trace))
        for trace in sorted(TRACES.glob("*.csv"))
    }
    if not expected:
        raise SystemExit("H6 gate: no normalized traces found")

    assert_tool_set(expected, TOOLS)
    assert_tool_set(expected, ("hbmsim", "ramulator2"), "-hbm3")

    comparison_path = RESULTS / "three-simulator-comparison.csv"
    if not comparison_path.exists():
        raise SystemExit(f"H6 gate: missing {comparison_path}")
    comparison = read_rows(comparison_path)
    if len(comparison) != len(expected) * len(TOOLS):
        raise SystemExit("H6 gate: incomplete three-simulator comparison table")
    hbm3_comparison = RESULTS / "hbm3-two-simulator-comparison.csv"
    if not hbm3_comparison.exists() or len(read_rows(hbm3_comparison)) != len(expected) * 2:
        raise SystemExit("H6 gate: incomplete HBM3 two-simulator comparison table")
    metadata = Path("validation/reference-inputs/hbm3-metadata.json").read_text(encoding="utf-8")
    if '"comparison_status": "unsupported"' not in metadata:
        raise SystemExit("H6 gate: HBM3 DRAMSys unsupported status is not explicit")
    print(f"H6 gate passed: HBM2 {len(expected)}x3 and HBM3 {len(expected)}x2")


if __name__ == "__main__":
    main()
