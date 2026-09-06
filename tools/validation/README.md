# Validation artifacts

`generate_microtraces.py` creates deterministic, normalized transaction CSVs. `summarize_hbmsim.py` runs the HBMSim binary, saves each completion stream, and emits `validation/results/hbmsim-summary.csv` with request count, mean/p95 latency, and throughput.

The GitHub Actions workflow uploads the `validation/` directory as `hbm-validation-results`. A reference metric is added to the same normalized table only after its adapter has processed the identical trace and topology; an unavailable reference remains absent rather than being estimated.
