"""Exercise the actual streaming frontend with bounded queues and late retries."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="hbmsim-cli-") as directory:
    root = Path(directory)
    trace = root / "trace.csv"
    output = root / "completed.csv"
    trace.write_text("0,WRITE,0,16384\n1,READ,0,4096\n2,WRITE,4096,4096\n"
                     "2,READ,8192,64\n3,READ,16384,32\n")
    result = subprocess.run([sys.argv[1], str(trace), "--profile", "hbm2_2000",
        "--queue-capacity", "1", "--completions", str(output)],
        capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(result.stderr)
    with output.open() as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 5 or {int(row["id"]) for row in rows} != set(range(1, 6)):
        raise RuntimeError("lost or duplicate completion")
    for row in rows:
        if int(row["latency_ps"]) != int(row["completion_ps"]) - int(row["arrival_ps"]):
            raise RuntimeError("backpressure lost original arrival")
    trace.write_text("2,READ,0,32\n1,READ,32,32\n")
    result = subprocess.run([sys.argv[1], str(trace)], capture_output=True, text=True)
    if result.returncode != 2 or "nondecreasing" not in result.stderr:
        raise RuntimeError("out-of-order trace was not rejected")
