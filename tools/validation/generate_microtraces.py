#!/usr/bin/env python3
"""Generate deterministic, profile-aware HBM transaction micro-traces."""

from __future__ import annotations

from pathlib import Path


OUT = Path("validation/traces")
PROFILES = ("hbm2_2000", "hbm3_6400")
SemanticAddress = tuple[int, int, int, int, int]
TraceRow = tuple[int, str, SemanticAddress, int]


def encode(profile: str, address: SemanticAddress) -> int:
    pseudo_channel, bank_group, bank, row, column = address
    if profile == "hbm2_2000":
        # HBM2 pseudo-channel BRC; SID/stack bit 10 is fixed to zero.
        return ((pseudo_channel << 5) | (bank_group << 6) | (bank << 8) |
                (column << 11) | (row << 16))
    # Inverse of HBMSim's Linear mapper for one channel and one SID.
    line = (((pseudo_channel * 4 + bank_group) * 4 + bank) * 16384 + row) * 32 + column
    return line * 32


def decode(profile: str, address: int) -> SemanticAddress:
    if profile == "hbm2_2000":
        return ((address >> 5) & 1, (address >> 6) & 3, (address >> 8) & 3,
                (address >> 16) & 0x3FFF, (address >> 11) & 0x1F)
    line = address // 32
    column = line % 32
    line //= 32
    row = line % 16384
    line //= 16384
    bank = line % 4
    line //= 4
    bank_group = line % 4
    line //= 4
    pseudo_channel = line % 2
    return pseudo_channel, bank_group, bank, row, column


def write_trace(profile: str, name: str, rows: list[TraceRow]) -> None:
    output = OUT / profile
    output.mkdir(parents=True, exist_ok=True)
    with (output / f"{name}.csv").open("w", encoding="utf-8") as stream:
        stream.write("# arrival_ps,op,address,size_bytes\n")
        for arrival, op, semantic, size in rows:
            address = encode(profile, semantic)
            if decode(profile, address) != semantic:
                raise AssertionError(f"{profile}: semantic address round trip failed: {semantic}")
            stream.write(f"{arrival},{op},0x{address:x},{size}\n")


def workloads() -> dict[str, list[TraceRow]]:
    interval = 10_000
    return {
        "row_hit": [
            (index * interval, "READ", (0, 0, 0, 7, index % 32), 32)
            for index in range(32)
        ],
        "row_conflict": [
            (index * interval, "READ",
             (0, 0, 0, 7 + index % 2, (index // 2) % 32), 32)
            for index in range(32)
        ],
        "bank_parallel": [
            (index * interval, "READ", (0, (index // 4) % 4, index % 4, 7, 0), 32)
            for index in range(32)
        ],
        "read_write_mix": [
            (index * interval, "WRITE" if index % 3 == 0 else "READ",
             (0, 0, 0, 7, index % 32), 32)
            for index in range(48)
        ],
        "sequential": [
            (index * interval, "READ", (0, 0, 0, 7 + index // 32, index % 32), 32)
            for index in range(64)
        ],
        "write_only": [
            (index * interval, "WRITE", (0, 0, 0, 7, index % 32), 32)
            for index in range(32)
        ],
        # 5 ns is exactly representable by both validation frontends and is
        # short enough to create sustained queueing pressure.
        "queue_pressure": [
            (index * 5_000, "READ", (index % 2, (index // 2) % 4,
                                      (index // 8) % 4, 7, index % 32), 32)
            for index in range(128)
        ],
    }


def main() -> None:
    traces = workloads()
    for profile in PROFILES:
        for name, rows in traces.items():
            write_trace(profile, name, rows)
    print(f"generated {len(traces)} semantic traces for {len(PROFILES)} profiles")


if __name__ == "__main__":
    main()
