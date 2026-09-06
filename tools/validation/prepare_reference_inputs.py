#!/usr/bin/env python3
"""Convert neutral micro-traces to Ramulator and DRAMSys input formats."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def read_trace(path: Path) -> list[dict[str, str]]:
    rows = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw and not raw.startswith("#"):
            arrival_ps, op, address, size = (item.strip() for item in raw.split(",")[:4])
            rows.append({"arrival_ps": arrival_ps, "op": op.upper(), "address": address, "size": size})
    return rows


def hbm2_vector(address: int) -> str:
    """Encode DRAMSys's supplied HBM2 BRC address bit layout for Ramulator."""
    pseudo_channel = (address >> 5) & 0x1
    bank_group = (address >> 6) & 0x3
    bank = (address >> 8) & 0x3
    sid = (address >> 10) & 0x1
    column = (address >> 11) & 0x1F
    row = (address >> 16) & 0x7FFF
    return f"0,{pseudo_channel},{sid},{bank_group},{bank},{row},{column}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dramsys-root", type=Path, required=True)
    args = parser.parse_args()
    trace_dir = Path("validation/traces")
    ramulator_dir = Path("validation/reference-inputs/ramulator2")
    dramsys_dir = Path("validation/reference-inputs/dramsys")
    ramulator_dir.mkdir(parents=True, exist_ok=True)
    dramsys_dir.mkdir(parents=True, exist_ok=True)

    for trace in sorted(trace_dir.glob("*.csv")):
        rows = read_trace(trace)
        with (ramulator_dir / f"{trace.stem}.trace").open("w", encoding="utf-8") as stream:
            for item in rows:
                op = "R" if item["op"] == "READ" else "W"
                stream.write(f"{op} {hbm2_vector(int(item['address'], 0))}\n")
        with (dramsys_dir / f"{trace.stem}.stl").open("w", encoding="utf-8") as stream:
            stream.write("# Generated from validation/traces; timestamps are 1 ns cycles.\n")
            for item in rows:
                cycle = int(item["arrival_ps"]) // 1_000
                op = "read" if item["op"] == "READ" else "write"
                stream.write(f"{cycle}: ({item['size']}) {op} {item['address']}\n")
        config = {
            "simulation": {
                "addressmapping": str((args.dramsys_root / "configs/addressmapping/am_hbm2_16Gb-8H_pc_brc.json").resolve()),
                "mcconfig": str((args.dramsys_root / "configs/mcconfig/fr_fcfs.json").resolve()),
                "memspec": str((args.dramsys_root / "configs/memspec/HBM2.json").resolve()),
                "simconfig": str((args.dramsys_root / "configs/simconfig/example.json").resolve()),
                "simulationid": f"hbmsim-validation-{trace.stem}",
                "tracesetup": [{"clkMhz": 1000, "dataLength": 64, "type": "player",
                                "name": str((dramsys_dir / f"{trace.stem}.stl").resolve()),
                                "maxPendingReadRequests": 128, "maxPendingWriteRequests": 128}],
            }
        }
        (dramsys_dir / f"{trace.stem}.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")

    metadata = {
        "neutral_trace_format": "arrival_ps,op,address,size_bytes",
        "ramulator2": {"standard": "HBM2_2Gb / HBM2_2000Mbps", "input": "ReadWriteTrace hierarchical address vector", "arrival_model": "one accepted request per 1 ns frontend tick"},
        "dramsys": {"standard": "stock configs/memspec/HBM2.json", "input": "absolute STL trace at 1000 MHz", "arrival_model": "arrival_ps quantized to 1 ns"},
        "comparison_status": "same operation/address sequence; timing and HBM2 organisation are not yet fully harmonized",
    }
    Path("validation/reference-inputs/metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
