# H6 reference validation

## Scope

The validation target is a common HBM2 micro-trace set run through HBMSim, Ramulator 2.1, and DRAMSys. The published comparison must report throughput, mean/p95 latency, and row hit/closed/conflict counts, and must identify every intentional abstraction mismatch.

## V0.2 common HBM2 profile

[`validation/profiles/hbm2_2000.json`](../validation/profiles/hbm2_2000.json) is the versioned profile contract. It pins the Ramulator HBM2 2Gb / 2000 Mbps timing basis, the pseudo-channel BRC address-bit layout used by DRAMSys, the neutral request format, and each tool's controller/refresh binding. HBMSim runs it explicitly with:

```sh
./build/hbmsim validation/traces/row_hit.csv --profile hbm2_2000
```

The manifest also names the remaining limitations that prevent a strict V0.3 claim: HBMSim currently reserves one channel-level data bus rather than two independent pseudo-channel buses and does not model HBM2 SID, Ramulator's trace frontend has no request-size field, and the stock DRAMSys HBM2 memory specification differs in density/topology. The Actions comparison carries this scope automatically through `validation/reference-inputs/metadata.json`; it must not be interpreted as numerical equivalence until those differences are removed.

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

## Local execution record — 2026-09-06

HBMSim built and passed all three local regression executables. The two reference builds were attempted from the pinned source trees, but no performance numbers are claimed because neither reference executable could be produced on this host:

| Reference | Attempt | Result |
| --- | --- | --- |
| Ramulator 2.1 Python binding | CMake with the system Python | blocked: macOS Python 3.9 is below upstream's 3.10 minimum |
| Ramulator 2.1 Python binding | bundled Python 3.12 | blocked: runtime lacks `Development.Module` headers required by upstream CMake |
| Ramulator 2.1 pure C++ library | bindings disabled | blocked: AppleClang 21 rejects the pinned fmt 10.2 consteval implementation and upstream `param.h` requires a dependent-template fix |
| DRAMSys | CLI build, trace analyzer disabled | blocked: bundled DRAMPower serializes `vector<bool>` through an invalid cast under current libc++ |

These are environment/toolchain failures, not validation results. No latency or bandwidth error is inferred from them.

## Reproducible Ubuntu validation gate

`.github/workflows/reference-validation.yml` runs on Ubuntu, checks out the recorded upstream revisions, builds HBMSim plus both references, and preserves their logs. It is the required gate before adding a numerical comparison table. The next implementation step is a normalizer that feeds the same HBM2 request stream to each tool and writes `results/reference-comparison.csv`; it must not fabricate missing DRAMSys HBM3/HBM4 capability.
