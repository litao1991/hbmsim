#!/usr/bin/env python3
"""Run the pinned Ramulator 2.1 binding and save raw plus normalized metrics."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def leaves(value: object, wanted: str) -> list[object]:
    if isinstance(value, dict):
        found: list[object] = []
        for key, child in value.items():
            if key == wanted:
                found.append(child)
            found.extend(leaves(child, wanted))
        return found
    if isinstance(value, list):
        return [entry for child in value for entry in leaves(child, wanted)]
    return []


def number(stats: dict, name: str) -> float:
    values = leaves(stats, name)
    return sum(float(value) for value in values) if values else 0.0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--drain-cycles", type=int, default=4096)
    parser.add_argument("--profile", type=Path,
                        default=Path("validation/profiles/hbm2_2000.json"))
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    if profile.get("profile_id") != "hbm2_2000":
        raise ValueError("Ramulator runner currently supports hbm2_2000 only")
    import ramulator

    trace_dir = Path("validation/reference-inputs/ramulator2")
    result_dir = Path("validation/results")
    result_dir.mkdir(parents=True, exist_ok=True)
    rows = []
    for trace in sorted(trace_dir.glob("*.trace")):
        dram = ramulator.dram.HBM2(org_preset="HBM2_2Gb", timing_preset="HBM2_2000Mbps")
        controller = ramulator.controller.HBM12(
            dram=dram, scheduler=ramulator.scheduler.FRFCFSRowHit(),
            refresh_manager=ramulator.refresh_manager.NoRefresh(),
            row_policy=ramulator.row_policy.Open(),
            addr_mapper=ramulator.addr_mapper.RoBaRaCoCh())
        memory = ramulator.memory_system.GenericDRAM(
            clock_ratio=1, channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
            controllers=[controller])
        simulation = ramulator.Simulation(
            ramulator.frontend.ReadWriteTrace(clock_ratio=1, path=str(trace.resolve())), memory)
        simulation._sim.run_with_drain(args.drain_cycles)
        simulation.finalize()
        stats = simulation.stats
        (result_dir / f"ramulator2-{trace.stem}-stats.json").write_text(
            json.dumps(stats, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        completed = int(number(stats, "num_read_reqs_served") + number(stats, "num_write_reqs_served"))
        rows.append({
            "tool": "ramulator2", "trace": trace.stem,
            "profile": profile["profile_id"],
            "requests": len(trace.read_text(encoding="utf-8").splitlines()),
            "completed_requests": completed,
            "mean_latency_ps": number(stats, "avg_read_latency") * 1000,
            "p95_latency_ps": "", "throughput_bytes_per_ns": "",
            "metric_note": f"mean is read-only; throughput omitted because {args.drain_cycles} fixed drain cycles are included",
        })
    with (result_dir / "ramulator2-summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
