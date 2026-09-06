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
    write_trace("row_hit", [(index * 50_000, "READ", index * 64, 64) for index in range(32)])
    write_trace("row_conflict", [
        (index * 50_000, "READ", (index % 2) * 8 * 1024, 64) for index in range(32)
    ])
    write_trace("bank_parallel", [
        (0, "READ", index * 1024, 64) for index in range(32)
    ])
    write_trace("read_write_mix", [
        (index * 25_000, "READ" if index % 3 else "WRITE", index * 64, 64)
        for index in range(48)
    ])
    write_trace("sequential", [(index * 10_000, "READ", index * 64, 64) for index in range(64)])


if __name__ == "__main__":
    main()
