#!/usr/bin/env python3
"""Convert neutral traces to timed Ramulator HBM3 hierarchical traces."""

from __future__ import annotations

import argparse
import json
from decimal import Decimal
from pathlib import Path

from prepare_reference_inputs import read_trace


def hbm3_vector(address: int) -> str:
    """Mirror HBMSim's Linear mapper for the one-channel HBM3 profile."""
    line = address // 32
    channel = 0
    column = line % 32
    line //= 32
    row = line % 16384
    line //= 16384
    bank = line % 4
    line //= 4
    bank_group = line % 4
    line //= 4
    pseudo_channel = line % 2
    sid = 0
    return f"{channel},{pseudo_channel},{sid},{bank_group},{bank},{row},{column}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", type=Path,
                        default=Path("validation/profiles/hbm3_6400.json"))
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    if profile.get("profile_id") != "hbm3_6400":
        raise ValueError("HBM3 preparation requires hbm3_6400")

    output = Path("validation/reference-inputs/ramulator2-hbm3")
    output.mkdir(parents=True, exist_ok=True)
    tick_ps = Decimal(str(profile["request_contract"]["arrival_time_unit_ps"]))
    request_size = profile["request_contract"]["request_size_bytes"]
    for trace in sorted(Path("validation/traces").glob("*.csv")):
        rows = read_trace(trace)
        with (output / f"{trace.stem}.trace").open("w", encoding="utf-8") as stream:
            for request_id, item in enumerate(rows, start=1):
                arrival_ps = Decimal(item["arrival_ps"])
                ticks = arrival_ps / tick_ps
                if ticks != ticks.to_integral_value():
                    raise ValueError(f"{trace}: arrival {arrival_ps} ps is not an HBM3 half-CK multiple")
                if int(item["size"], 0) != request_size:
                    raise ValueError(f"{trace}: HBM3 validation requires {request_size} B requests")
                op = "R" if item["op"] == "READ" else "W"
                stream.write(f"{int(ticks)} {op} {request_id} "
                             f"{hbm3_vector(int(item['address'], 0))}\n")

    metadata = {
        "profile_id": profile["profile_id"],
        "profile_manifest": str(args.profile),
        "neutral_trace_format": profile["request_contract"]["trace_format"],
        "ramulator2": {
            "standard": "HBM3_16Gb_4hi / HBM3_6400Mbps",
            "input": "ReadWriteTrace hierarchical address vector",
            "arrival_model": "absolute arrivals in 312.5 ps half-CK ticks"
        },
        "dramsys": profile["tool_bindings"]["dramsys"],
        "comparison_status": profile["comparison_status"]
    }
    Path("validation/reference-inputs/hbm3-metadata.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
