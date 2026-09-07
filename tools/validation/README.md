# Validation artifacts

`validation/profiles/hbm2_2000.json` is the machine-readable V0.2 profile contract. It names the request format, organization, timing, mapping bits, each tool binding, and the remaining non-equivalence explicitly. `generate_microtraces.py` creates deterministic transaction CSVs. `summarize_hbmsim.py` runs the HBMSim binary with `--profile hbm2_2000`, saves each completion stream, and emits `validation/results/hbmsim-summary.csv` with request count, mean/p95 latency, and throughput.

The GitHub Actions workflow converts the same neutral CSV sequence for Ramulator 2.1 and DRAMSys, runs all three tools, and uploads `validation/` as `hbm-validation-results`. The entry point is `validation/results/three-simulator-comparison.csv`; raw Ramulator JSON statistics, DRAMSys TDB databases/logs, converted inputs, and configuration metadata are kept beside it for audit.

This is a V0.2 common-profile comparison, not yet a claim of strict simulator equivalence. The manifest records the remaining HBMSim pseudo-channel data-bus limitation and the DRAMSys stock-spec density difference. The consolidated table carries that scope note. Ramulator's throughput is blank because its bounded post-input drain is necessary to collect completion latency and would otherwise distort elapsed-time throughput.
