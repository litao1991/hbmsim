#!/usr/bin/env python3
"""Run the pinned Ramulator 2.1 binding and save raw plus normalized metrics."""

from __future__ import annotations

import argparse
import csv
import json
import math
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


def percentile(values: list[int], percentage: int) -> float:
    ordered = sorted(values)
    return float(ordered[max(0, math.ceil(percentage / 100 * len(ordered)) - 1)])


def main() -> None:
    parser = argparse.ArgumentParser()
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
        command_file = result_dir / f"ramulator2-{trace.stem}-commands.csv"
        controller = ramulator.controller.HBM12(
            dram=dram, scheduler=ramulator.scheduler.FRFCFSRowHit(),
            refresh_manager=ramulator.refresh_manager.NoRefresh(),
            row_policy=ramulator.row_policy.Open(),
            # The timed frontend supplies HBM2's complete hierarchical vector
            # [Channel, PseudoChannel, SID, BankGroup, Bank, Row, Column].
            # A flat-address mapper would overwrite it with req.addr (-1).
            addr_mapper=ramulator.addr_mapper.PassThroughAddrMapper(),
            controller_plugins=[ramulator.controller_plugin.CommandCounter(
                commands_to_count=["ACT", "PREpb", "RD", "WR"], path=str(command_file.resolve()))])
        memory = ramulator.memory_system.GenericDRAM(
            clock_ratio=1, channel_mapper=ramulator.channel_mapper.PassThroughChannelMapper(),
            controllers=[controller])
        completion_file = result_dir / f"ramulator2-{trace.stem}-completions.csv"
        frontend = {
            "impl": "ReadWriteTrace", "clock_ratio": 1, "path": str(trace.resolve()),
            "completion_path": str(completion_file.resolve()),
        }
        simulation = ramulator.Simulation(frontend, memory)
        simulation.run()
        simulation.finalize()
        stats = simulation.stats
        (result_dir / f"ramulator2-{trace.stem}-stats.json").write_text(
            json.dumps(stats, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        with completion_file.open(newline="", encoding="utf-8") as stream:
            completions = list(csv.DictReader(stream))
        with command_file.open(newline="", encoding="utf-8") as stream:
            commands = {row[0].strip(): row[1].strip() for row in csv.reader(stream)}
        requests = len(trace.read_text(encoding="utf-8").splitlines())
        if len(completions) != requests:
            raise RuntimeError(f"{trace}: completed {len(completions)} of {requests} requests")
        latencies = [int(row["latency_cycles"]) * 1000 for row in completions]
        first_arrival = min(int(row["arrival_cycle"]) for row in completions) * 1000
        last_completion = max(int(row["completion_cycle"]) for row in completions) * 1000
        rows.append({
            "tool": "ramulator2", "trace": trace.stem,
            "profile": profile["profile_id"],
            "requests": requests, "completed_requests": len(completions),
            "mean_latency_ps": sum(latencies) / len(latencies),
            "p50_latency_ps": percentile(latencies, 50),
            "p95_latency_ps": percentile(latencies, 95),
            "throughput_bytes_per_ns": requests * profile["request_contract"]["request_size_bytes"] * 1000 /
                                   max(1, last_completion - first_arrival),
            "act_commands": commands.get("ACT", "0"), "pre_commands": commands.get("PREpb", "0"),
            "read_commands": commands.get("RD", "0"), "write_commands": commands.get("WR", "0"),
            "row_hits": int(number(stats, "row_hits")),
            "row_misses": int(number(stats, "row_misses")),
            "row_conflicts": int(number(stats, "row_conflicts")),
            "metric_note": "callback-derived completion time; absolute arrivals preserved at 1 ns",
        })
    with (result_dir / "ramulator2-summary.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


if __name__ == "__main__":
    main()
