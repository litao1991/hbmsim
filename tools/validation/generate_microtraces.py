#!/usr/bin/env python3
"""Generate deterministic, simulator-neutral HBM transaction micro-traces."""

from pathlib import Path


OUT = Path("validation/traces")


def write_trace(name: str, rows: list[tuple[int, str, int, int]]) -> None:
    with (OUT / f"{name}.csv").open("w", encoding="utf-8") as stream:
        stream.write("# arrival_ps,op,address,size_bytes\n")
        for arrival, op, address, size in rows:
            stream.write(f"{arrival},{op},0x{address:x},{size}\n")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    # V0.3 uses exactly one 32 B HBM2 pseudo-channel payload per request and
    # 10 ns-spaced arrivals.  Every target can preserve these arrival times
    # without frontend backpressure or tool-specific burst expansion.
    write_trace("row_hit", [(index * 10_000, "READ", (index % 32) << 11, 32)
                            for index in range(32)])
    write_trace("row_conflict", [
        (index * 10_000, "READ", (index % 2) << 16, 32) for index in range(32)
    ])
    write_trace("bank_parallel", [
        (index * 10_000, "READ", ((index % 4) << 8) | (((index // 4) % 4) << 6), 32)
        for index in range(32)
    ])
    write_trace("read_write_mix", [
        (index * 10_000, "READ" if index % 3 else "WRITE", (index % 32) << 11, 32)
        for index in range(48)
    ])
    write_trace("sequential", [
        (index * 10_000, "READ", ((index % 32) << 11) | ((index // 32) << 16), 32)
        for index in range(64)
    ])


if __name__ == "__main__":
    main()
