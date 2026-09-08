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

H0–H6 and the v0.4–v0.6 architecture convergence are implemented. Each physical channel owns an independent `HbmController` with finite read/write queues, bank and refresh state, command/array/data resources, and diagnostics. `HbmSystem` retains only transaction splitting, global events, admission/backpressure, and parent completion. All scheduler choices use `IScheduler`; row, refresh, and mapping decisions use their policy interfaces.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/hbmsim traces/h0_smoke.csv
```

The canonical `Hbm2Standard` and `Hbm3Standard` objects provide organization, cycle-derived timing, commands, prerequisites, transitions, and mapping. Command coverage includes bank/all-bank precharge, auto-precharge, all/per-bank refresh, and HBM3/HBM4 RFM. Transport uses exact rational rates rather than rounded bytes/ns. GitHub Actions permanently gates HBM2 numerical drift and request completion across HBMSim/Ramulator/DRAMSys, plus HBM3 completion across HBMSim/Ramulator; the pinned DRAMSys version is explicitly marked unsupported for HBM3. See [docs/H6_REFERENCE_VALIDATION.md](docs/H6_REFERENCE_VALIDATION.md).
