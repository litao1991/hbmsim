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


def write_dramsys_memspec(profile: dict, dramsys_root: Path, output: Path) -> None:
    """Derive the DRAMSys HBM2 JSON from the shared profile, not stock density."""
    source = dramsys_root / "configs/memspec/HBM2.json"
    memspec = json.loads(source.read_text(encoding="utf-8"))
    organization = profile["hbm2_organization"]
    timing = profile["timing_ps"]
    architecture = memspec["memspec"]["memarchitecturespec"]
    architecture.update({
        "nbrOfBankGroups": organization["bank_groups_per_pseudo_channel"],
        "nbrOfBanks": organization["bank_groups_per_pseudo_channel"] *
                      organization["banks_per_bank_group"],
        "nbrOfColumns": organization["columns_per_row"],
        "nbrOfStacks": 1,
        "nbrOfPseudoChannels": organization["pseudo_channels_per_channel"],
        "nbrOfRows": organization["rows_per_bank"],
        "nbrOfChannels": organization["channels"],
    })
    timings = memspec["memspec"]["memtimingspec"]
    cycle = organization["tck_ps"]
    for target, source_name in {
        "RCDRD": "t_rcd", "RCDWR": "t_rcd", "RP": "t_rp", "RAS": "t_ras",
        "RC": "t_rc", "CCDS": "t_ccd", "RRDS": "t_rrd", "RRDL": "t_rrd",
        "FAW": "t_faw", "WTRS": "t_wtr", "RTW": "t_rtw", "RFC": "t_rfc",
        "RFCSB": "t_rfcpb",
    }.items():
        timings[target] = timing[source_name] // cycle
    timings.update({
        "CCDL": 4, "CCDR": 2, "RL": 14, "WL": 5, "WR": 16, "RTP": 5,
        "WTRL": 8, "REFI": 3900, "REFISB": 243, "RREFD": 8, "tCK": cycle * 1e-12,
    })
    output.write_text(json.dumps(memspec, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dramsys-root", type=Path, required=True)
    parser.add_argument("--profile", type=Path,
                        default=Path("validation/profiles/hbm2_2000.json"))
    args = parser.parse_args()
    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    if profile.get("profile_id") != "hbm2_2000":
        raise ValueError("reference input preparation currently supports hbm2_2000 only")
    trace_dir = Path("validation/traces")
    ramulator_dir = Path("validation/reference-inputs/ramulator2")
    dramsys_dir = Path("validation/reference-inputs/dramsys")
    ramulator_dir.mkdir(parents=True, exist_ok=True)
    dramsys_dir.mkdir(parents=True, exist_ok=True)
    dramsys_memspec = dramsys_dir / "hbm2_2000-memspec.json"
    write_dramsys_memspec(profile, args.dramsys_root, dramsys_memspec)

    request_contract = profile["request_contract"]
    request_size = request_contract["request_size_bytes"]
    arrival_unit = request_contract["arrival_time_unit_ps"]
    for trace in sorted(trace_dir.glob("*.csv")):
        rows = read_trace(trace)
        with (ramulator_dir / f"{trace.stem}.trace").open("w", encoding="utf-8") as stream:
            for request_id, item in enumerate(rows, start=1):
                arrival_ps = int(item["arrival_ps"])
                if arrival_ps % arrival_unit != 0:
                    raise ValueError(f"{trace}: arrival {arrival_ps} ps is not a profile clock multiple")
                if int(item["size"], 0) != request_size:
                    raise ValueError(f"{trace}: V0.3 requires {request_size} B requests")
                op = "R" if item["op"] == "READ" else "W"
                stream.write(f"{arrival_ps // arrival_unit} {op} {request_id} "
                             f"{hbm2_vector(int(item['address'], 0))}\n")
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
                "memspec": str(dramsys_memspec.resolve()),
                "simconfig": str((args.dramsys_root / "configs/simconfig/example.json").resolve()),
                "simulationid": f"hbmsim-validation-{trace.stem}",
                "tracesetup": [{"clkMhz": 1000, "dataLength": 64, "type": "player",
                                "name": str((dramsys_dir / f"{trace.stem}.stl").resolve()),
                                "maxPendingReadRequests": 128, "maxPendingWriteRequests": 128}],
            }
        }
        (dramsys_dir / f"{trace.stem}.json").write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")

    metadata = {
        "profile_id": profile["profile_id"],
        "profile_manifest": str(args.profile),
        "neutral_trace_format": profile["request_contract"]["trace_format"],
        "ramulator2": {"standard": "HBM2_2Gb / HBM2_2000Mbps", "input": "ReadWriteTrace hierarchical address vector", "arrival_model": "one accepted request per 1 ns frontend tick"},
        "dramsys": {"standard": "profile-derived HBM2.json", "input": "absolute STL trace at 1000 MHz", "arrival_model": "arrival_ps quantized to 1 ns"},
        "comparison_status": profile["comparison_status"],
    }
    Path("validation/reference-inputs/metadata.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
