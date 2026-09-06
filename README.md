# HBMSim

HBMSim is an event-driven, transaction-level HBM memory-system simulator with command-aware DRAM timing. It is designed as an independent peer of HBFSim and will later integrate through a small transaction interface while keeping HBM and HBF as separate memory-management domains.

## Design position

HBMSim combines three deliberately separate ideas:

- Ramulator 2.1 is the behavioural and timing reference for HBM organization, commands, controller policies, and constraint coverage.
- DRAMSys informs the transaction-oriented external contract and analysis discipline, without importing SystemC/TLM into the simulator.
- HBFSim's discrete-event simulator is the eventual system-time authority. HBMSim itself must advance to meaningful controller wakeups and completions, never through empty HBM clock ticks.

The retained first-version structure is `Stack → Channel → PseudoChannel → BankGroup → Bank → Row`. Public clients submit reads and writes; ACT, PRE, RD, WR, and REF remain private controller commands.

## Repository layout

```text
docs/                         architecture and delivery plans
third_party/reference/        pinned, read-only upstream study sources
  ramulator2/                 timing and controller behaviour oracle
  DRAMSys/                    transaction-interface and validation reference
```

`third_party/reference` is intentionally excluded from future product builds. It is an auditable source-study area, not vendored production code. See [docs/ITERATIVE_DEVELOPMENT_PLAN.md](docs/ITERATIVE_DEVELOPMENT_PLAN.md) for milestones and acceptance gates, and [docs/REFERENCE_BASELINES.md](docs/REFERENCE_BASELINES.md) for exact source revisions.

## Status

H0–H5 are implemented: the project builds as a standalone C++20 library and trace tool, exposes transaction submission/completion, uses picosecond time, maps every access through Stack/Channel/PseudoChannel/BankGroup/Bank/Row, and uses controller-wakeup events rather than clock ticks. It includes ACT/PRE/RD/WR planning, declarative timing constraints, FR-FCFS scheduling, Open/Closed row policy, write drain, refresh, and optional topology-safe simulation-access aggregation.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/hbmsim traces/h0_smoke.csv
```

H6 adds all-bank/per-bank refresh and the HBM4 `RFMpb` baseline. V0.1 correctness hardening is complete: idle refresh cannot leave an invalid open-row hit, and coalescing cannot cross a mapped memory resource. V0.2 will introduce a common HBM2 profile manifest; V0.3 will publish strict, like-for-like numerical comparison. The exact source alignment and current reference-build status are documented in [docs/H6_REFERENCE_VALIDATION.md](docs/H6_REFERENCE_VALIDATION.md).
