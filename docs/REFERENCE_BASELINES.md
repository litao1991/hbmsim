# Open-source reference baselines

HBMSim is a clean, event-driven implementation. The checked-out projects below are **read-only study baselines**; no source file is compiled or copied into HBMSim without an explicit future decision and license review.

| Project | Local path | Pinned revision | Intended use | Not adopted |
| --- | --- | --- | --- | --- |
| Ramulator 2.1 | `third_party/reference/ramulator2` | `72427a1bba3771564c4fb0e494ba02242fd1eaa7` | HBM hierarchy, command/state semantics, timing constraints, controller composition, regression concepts | its cycle-by-cycle global simulation loop |
| DRAMSys | `third_party/reference/DRAMSys` | `c8475150551e4348434b56f5111a966bdf0d81f9` | transaction-facing boundary, configurable mapping/page/refresh policies, cross-validation methodology | SystemC/TLM runtime and its execution model |

## Study map

Ramulator 2.1 should be consulted in this order during implementation:

1. `src/ramulator/dram/impl/HBM2.cpp` and `HBM3.cpp`: organization, command prerequisiites, state transitions, timing tables.
2. `src/ramulator/dram/impl/HBM4.cpp` and `python/ramulator/dram/hbm4.py`: forward-compatible command and timing vocabulary; HBM4 itself remains out of scope for v0.1.
3. `src/ramulator/controller/`: request-buffer, scheduler, row-policy, refresh, and controller decomposition.
4. `tests/latency_throughput/` and `tests/smoke/`: validation trace patterns and regression shape.

DRAMSys should be consulted for its decoupled request/response model, address mapping configuration, page policy boundaries, and metric collection. Its SystemC dependencies must not enter HBMSim.

## Reproducibility

Both references were shallow-cloned on 2026-09-06 from their public upstream GitHub repositories. Before updating either baseline, record the new commit hash here and rerun the validation matrix described in the development plan. Upstream licenses and notices remain in each checked-out repository and apply to those directories.
