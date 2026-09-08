# H6 reference validation

## Scope

The validation target is one common micro-trace set. HBM2 runs through HBMSim, Ramulator 2.1, and DRAMSys; HBM3 runs through HBMSim and Ramulator 2.1. Both published comparisons report throughput, mean/p50/p95 latency, commands and row locality, and identify intentional abstraction mismatches.

## V0.2 common HBM2 profile

[`validation/profiles/hbm2_2000.json`](../validation/profiles/hbm2_2000.json) is the versioned profile contract. It pins the Ramulator HBM2 2Gb / 2000 Mbps timing basis, the pseudo-channel BRC address-bit layout used by DRAMSys, the neutral request format, and each tool's controller/refresh binding. HBMSim runs it explicitly with:

```sh
./build/hbmsim validation/traces/hbm2_2000/row_hit.csv --profile hbm2_2000
```

The manifest fixes the SID/stack selector to zero, matching the single-SID Ramulator HBM2_2Gb baseline without introducing a superfluous HBMSim hierarchy. The validation input preparer derives DRAMSys HBM2 memspec and dense address-mapping JSON from that same organization and timing contract instead of using its stock 16Gb/8Hi dimensions. Its STL conversion removes only that constant SID bit, so all dynamic burst, pseudo-channel, bank-group, bank, column, and row fields remain identical. The Actions comparison carries the selected profile through `validation/reference-inputs/metadata.json`.

Ramulator receives the same fields as a hierarchical vector in the order `Channel, PseudoChannel, SID, BankGroup, Bank, Row, Column`. It uses `PassThroughAddrMapper`; a flat-address mapper would overwrite this vector and invalidate bank/row-locality comparison.

## V0.3 alignment work

HBMSim now reserves data transfer independently for each pseudo-channel. The common microtraces use a single 32 B HBM2 pseudo-channel payload at every request, so no tool-specific burst coalescing is required. The validation workflow applies [`patch_ramulator_timed_trace.py`](../tools/validation/patch_ramulator_timed_trace.py) to the pinned checkout before building it. The adapter changes only the test frontend: it reads absolute 1 ns arrival cycles, retains the request identity, writes callback-derived completion records and stops only after every request completes. It does not alter Ramulator's HBM2 DRAM model, controller, scheduler or timing rules.

The three summaries now have fields for submitted/completed request counts, mean/p50/p95 latency, throughput, ACT/PRE/RD/WR totals and row hit/miss/conflict. DRAMSys derives the latter from its recorded command phases. Its pinned stock controller closes rows, unlike the open-row HBMSim/Ramulator binding, so DRAMSys locality is published but excluded from the open-row trend assertion. It remains a comparison result rather than an assumption of cycle identity.

## HBM4/RFM source alignment

HBMSim's `HbmTimingSpec::hbm4_8000()` is transcribed from the public Ramulator 2.1 `python/ramulator/dram/hbm4.py` preset `HBM4_8000Mbps` at pinned revision `72427a1bba3771564c4fb0e494ba02242fd1eaa7`.

| Parameter | Ramulator HBM4-8000 definition | HBMSim value |
| --- | --- | --- |
| `tCK` | 500 ps | 500 ps |
| `nRCDRD` | 39 cycles | 19,500 ps |
| `nRP` | 33 cycles | 16,500 ps |
| `nCL` | 20 cycles | 10,000 ps |
| `nRAS` | 57 cycles | 28,500 ps |
| `nFAW` | 30 cycles | 15,000 ps |
| `nRFC` (32Gb, 8Hi) | 450 ns | 450,000 ps |
| `nRFCpb` (32Gb) | 280 ns | 280,000 ps |
| `nRFMab` / `nRFMpb` | aliases of refresh time | same as `tRFC` / `tRFCpb` |

HBMSim models `REFab`, `REFpb`, and threshold-triggered `RFMpb`. `RFMab`, SID topology, auto-precharge commands, and proprietary RFM policy heuristics are intentionally outside this baseline.

## v0.6.4 semantic and permanent gates

The trace generator constructs seven workloads from semantic `(PseudoChannel, BankGroup, Bank, Row, Column)` addresses, then applies a profile-specific inverse mapper. HBM2 and HBM3 therefore exercise the same row-hit, row-conflict, bank-parallel, mixed, sequential, write-only, and queue-pressure intent even though their flat-address mappings differ.

`validation/baselines/hbmsim-hbm2_2000-v0.6.4.csv` freezes the new HBM2 results. CI requires exact request/command/row classifications and a tight numerical match for mean/p50/p95 latency and throughput. The semantic gate also proves that every logical completion is represented by exactly one physical data command or a declared same-address merge, and that queue wait + command phase + data-ready + data-bus wait + data service equals end-to-end latency.

`validation/profiles/hbm3_6400.json` aligns one 32 B request, absolute arrival time and hierarchical address between HBMSim and Ramulator. Ramulator models one tick as half an HBM3 CK (312.5 ps). Every input request must complete in both tools before `hbm3-two-simulator-comparison.csv` is accepted. The pinned DRAMSys release does not expose an HBM3 standard and is therefore explicitly excluded.

## Reproducible Ubuntu validation gate

`.github/workflows/reference-validation.yml` runs on Ubuntu, checks out the recorded upstream revisions, builds HBMSim plus both references, produces the HBM2 three-simulator and HBM3 two-simulator tables, enforces semantic, frozen-number, reference-trend, and completion gates, and uploads all normalized and raw validation artifacts.
