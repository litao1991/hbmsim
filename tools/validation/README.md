# Validation artifacts

`generate_microtraces.py` creates deterministic, normalized transaction CSVs. `summarize_hbmsim.py` runs the HBMSim binary, saves each completion stream, and emits `validation/results/hbmsim-summary.csv` with request count, mean/p95 latency, and throughput.

The GitHub Actions workflow converts the same neutral CSV sequence for Ramulator 2.1 and DRAMSys, runs all three tools, and uploads `validation/` as `hbm-validation-results`. The entry point is `validation/results/three-simulator-comparison.csv`; raw Ramulator JSON statistics, DRAMSys TDB databases/logs, converted inputs, and configuration metadata are kept beside it for audit.

This is an initial *same request sequence* comparison, not a claim of strict simulator equivalence: HBMSim's current default HBM3-like profile, Ramulator's HBM2_2Gb preset, and DRAMSys's supplied HBM2 configuration do not yet share every timing/topology/mapping parameter. The consolidated table deliberately carries that scope note. Ramulator's throughput is blank because its bounded post-input drain is necessary to collect completion latency and would otherwise distort elapsed-time throughput.
