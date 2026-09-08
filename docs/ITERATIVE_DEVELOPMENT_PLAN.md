# HBMSim iterative development plan

## Goal and non-goals

HBMSim will be an **event-driven, transaction-level HBM simulator with command-aware DRAM timing**. An external user sees a timed `READ` or `WRITE` completion. Internally, a controller resolves the required ACT/PRE/RD/WR/REF command sequence and applies HBM timing and shared-resource constraints.

The initial release targets HBM2/HBM3 behaviour relevant to AI-system studies, while retaining a bounded HBM4/RFM baseline for reference validation. Pin-level PHY behaviour, DQ/DQS/CA signalling, training, ECC, thermal effects, and power-down are deliberately deferred.

The unit of simulated time is picoseconds. All configured timing parameters are normalized from cycles through `tCK` at configuration load, so HBM sub-nanosecond timing and HBF microsecond timing can later share one integer timeline without rounding.

## System boundary

```text
Client / AI runtime
        │ HbmTransaction {id, op, address, size, arrival_time, client}
        ▼
HbmSystem → splitter → address mapper → per-controller queues
        │                                  │
        │                       scheduler → command planner
        │                                  │
        └──── HbmCompletion ◄ timing engine / resource state
```

The first stable integration surface is deliberately small:

```cpp
RequestToken submit(const HbmTransaction& transaction);
void set_completion_callback(CompletionCallback callback);
```

It can later satisfy a shared HBFSim `IMemoryTarget` contract, but no common HBM/HBF media model will be introduced.

## Incremental delivery plan

## v0.4–v0.6 execution sequence

| Version | Core objective | Required exit gate |
| --- | --- | --- |
| v0.4 | Controller / Scheduler / Row / Refresh / Mapper architecture convergence | Extract the HBM controller-owned state behind policy interfaces. The default FR-FCFS must use the same `IScheduler` candidate path as FIFO and FRFCFSRowHit. Golden HBM traces retain bit-for-bit completion times/order, command counts, row classes, refresh/RFM counts and data-bus occupancy. |
| v0.5 | HBM2/HBM3 standards and complete timing | Replace the loose standard enum/configuration with canonical HBM organization, command and cycle timing profiles. Add HBM-specific `RCDRD/RCDWR`, `CCDS/CCDL`, `RRDS/RRDL`, `WTRS/WTRL`, `RTP` and `WR` constraints, then validate profile adapters against pinned references. |
| v0.6 | Resource/queue/stats and H6 re-validation | Finish controller resource ownership, queue/statistics reporting and the extended normalized trace suite. Re-run HBMSim, Ramulator 2.1 and DRAMSys on the latest head, including pseudo-channel parallelism and refresh collisions, before freezing a new baseline. |

### v0.4.1–v0.6.3 completion record

| Slice | Status | Implemented result |
| --- | --- | --- |
| v0.4.1 | Complete | One `HbmController` per channel owns queues, banks, timing history, refresh/RFM state and command/data resources. `HbmSystem` owns only the global event queue, splitter, atomic reservations and parent completion. |
| v0.5.1 | Complete | `Hbm2Standard`/`Hbm3Standard` are the canonical organization, speed-bin, mapping, command and transition sources; cycle values resolve once to picoseconds. |
| v0.5.2 | Complete | `PREpb/PREab`, `RDA/WRA`, `REFab/REFpb`, `RFMab/RFMpb`, command prerequisites, state transitions, SID scope and command duration are represented explicitly. |
| v0.6.1 | Complete | Exact rational bandwidth, finite read/write capacities with atomic backpressure, command/array/data accounting and starvation thresholds are implemented. |
| v0.6.2 | Complete | Queue, command, array, data-bus and refresh wait totals plus bank/pseudo-channel/channel busy statistics are emitted by the CLI. |
| v0.6.3 | Complete | CI freezes HBMSim HBM2 v0.3 values, reruns the HBM2 three-simulator gate, and adds an HBM3 HBMSim/Ramulator gate. DRAMSys HBM3 is explicitly unsupported. |

The versions are intentionally sequential: v0.4 is behavior-preserving only; v0.5 changes standard semantics only after v0.4's golden gate; v0.6 is the reference-validation gate. HBF, compute and AI workload work remain downstream of all three.

| Increment | Primary outcome | Scope | Exit criteria |
| --- | --- | --- | --- |
| H0 — foundation | HBFSim-compatible transaction boundary | `SimTime`, request/completion types, configuration loading, topology shell, deterministic event adapter, fixed latency plus channel bandwidth reservation | Unit tests prove monotonic completion times, deterministic same-time ordering, and channel-bandwidth contention; a standalone trace smoke test produces CSV metrics. |
| H1 — topology and locality | HBM hierarchy becomes explicit | Stack/Channel/PseudoChannel/BankGroup/Bank/Row mapping, access splitter, Bank open-row state, row-hit/closed/conflict latency | Mapping tests cover every hierarchy boundary; three directed traces show hit < closed < conflict latency and independent banks overlap. |
| H2 — command-aware timing | Complete | `CommandPlanner`, command/state transitions for ACT/PRE/RD/WR, declarative timing constraints, controller wakeup calculation | Directed tests cover `tRCD`, `tRP`, `tRAS`, `tRC`, `tCCD`; controller wakes only at the next eligible command/completion, never every clock cycle. |
| H3 — contention-aware controller | Complete baseline | Separate read/write queues, FR-FCFS, Open/Closed row policy interfaces, data-bus reservations, configurable write-drain threshold | Tests show ready > row-hit > age ordering and write-drain selection; queue-depth sampling and formal starvation bounds remain H6 validation work. |
| H4 — HBM timing completeness | Complete baseline | `tRRD`, `tFAW`, `tWTR`, `tRTW`, bank-group/pseudo-channel/channel scopes, all-bank and per-bank refresh | Constraint matrix tests include `tRCD` and `tFAW`; all-bank refresh blocks only the selected Channel and per-bank refresh rotates the selected bank. |
| H5 — scalable AI transactions | Complete baseline | Physical-burst versus simulation-access granularity, topology-safe 4 KiB/16KiB aggregation, bounded timing history and statistics | A directed 16 KiB transaction is represented by four 4 KiB accesses when the mapping permits it; aggregation never crosses a channel, pseudo-channel, bank group, bank, or row. 16 MiB scale and oracle calibration are explicit H6 gates. |
| H6 — oracle validation | Complete — V0.3 baseline frozen | Normalized trace format, runners for Ramulator 2.1 and DRAMSys where supported, metric comparer, stored reference configurations | Ubuntu CI builds and runs the pinned references. `hbm2_2000` makes the request, organization, timing and mapping contract auditable; V0.3 establishes strict arrival/burst/completion alignment and publishes numerical comparison results. Tool-specific command and row-locality differences are retained as measured results, not hidden. |
| H7 — HBFSim system integration | HBM/HBF/compute studies | Shared event-queue adapter, `IMemoryTarget` trial interface, HBF→HBM fill, residency/prefetch hooks, overlap statistics | One end-to-end scenario proves compute, HBM, and HBF events share a timeline; no HBM-specific dependency leaks into HBF media code. |

### V0 validation hardening

| Stage | Status | Deliverable |
| --- | --- | --- |
| V0.1 — Correctness | Complete | Refresh state is retained while a channel is idle and caught up at the next arrival; splitting observes physical-burst and address-mapping boundaries before coalescing only contiguous work in one channel/pseudo-channel/bank-group/bank/row resource; directed regressions cover these cases. |
| V0.2 — Common HBM2 profile | Complete | `validation/profiles/hbm2_2000.json` records organization, timing, mapping, burst and arrival semantics. HBMSim exposes it through `--profile hbm2_2000`; reference-input preparation records the manifest identity with every run. Known non-equivalences remain explicit rather than hidden. |
| V0.3 — Strict H6 | Complete | One normalized 32 B request stream with absolute 1 ns arrivals, full read/write completion accounting, command and row-locality metrics, plus p50/p95 latency and throughput comparison across all runnable tools. CI applies an auditable timed-trace adapter to the pinned Ramulator checkout; the frozen baseline is published in [run 34081337722](https://github.com/litao1991/hbmsim/actions/runs/34081337722). |

## Implementation rules

1. Keep public requests at transaction level; only controllers may see DRAM commands.
2. Preserve pseudo-channels and bank groups from H0 even where an early timing profile does not distinguish them.
3. Express timing as data (`preceding command`, `following command`, hierarchy scope, distance, delay), not conditionals distributed through the controller.
4. Schedule only controller wakeups and externally visible completions. Updating command history must be local state work, not a new global event by default.
5. Make policies substitutable: address mapper, scheduler, row policy, refresh manager, and timing standard own separate interfaces.
6. Add a directed regression before every timing or state-machine extension; do not silently alter a validated H0–H6 behaviour.

## First implementation slice (H0)

H0 should be split into five small pull requests:

1. Create `include/hbmsim/common` with picosecond `SimTime`, strong IDs, transaction, completion, and error/status types.
2. Create `kernel` adapter interfaces that let a caller schedule a callback at an absolute simulation time; provide a minimal standalone event queue for HBMSim-only tests.
3. Add topology/config parsing for a single stack and its channel/pseudo-channel counts; validate power-of-two assumptions only where the chosen address mapper requires them.
4. Implement `HbmSystem::submit`, deterministic request admission, a fixed-array-latency model, and per-channel data-bus reservation (`bytes / configured bandwidth`).
5. Add CSV trace replay plus `summary.csv`, latency histogram, channel occupancy, accepted/rejected request, and outstanding-request metrics.

H0 is complete only after it can be built as an independent C++20 library and driven by a small HBFSim adapter test. Do not start H1 by directly coupling to HBFSim internals.

## Validation matrix

Every timing milestone adds or updates the following classes of deterministic traces:

| Class | What it isolates |
| --- | --- |
| single-row hit / closed / conflict | command prerequisite and row-state correctness |
| same-bank sequence | bank timing history |
| bank-group and pseudo-channel sequence | scoped timing constraints |
| independent-bank fan-out | parallelism and resource ownership |
| sequential versus random | mapping, locality, and scheduler behaviour |
| read/write alternation | bus direction turn-around and write drain |
| refresh collision | maintenance interference and liveness |
| large contiguous AI object | splitting and aggregation scalability |

Run HBMSim and Ramulator 2.1 from the same normalized trace whenever that trace is in the common feature subset. Use DRAMSys as a second reference for HBM1/2 cases it publicly supports. Compare throughput, latency distribution, row hit/miss/conflict counts, per-channel utilization, and per-bank utilization. Exact cycle equality is not a goal; explain material trend differences in a checked-in validation report.

## Ownership and dependency sequence

`common → kernel adapter → topology/mapping → media state → timing → controller → refresh/stats → integration` is the intended dependency direction. The controller must not own global time; the caller/event adapter does. HBFSim integration is a downstream consumer and must not block the independent H0–H6 validation path.

## Decision checkpoints

- After H0: confirm the final HBFSim event-adapter API before any shared code is extracted.
- After H2: compare the declarative timing engine against Ramulator HBM2 directed traces; revise the data model if a constraint requires controller special-casing.
- After H4: decide whether HBM3 is sufficiently calibrated to become the default research configuration.
- After H5: select the default simulation access granularity from accuracy-versus-scale data, not by convention.
- After H6: freeze a validated baseline and version its configuration/trace suite before HBM/HBF placement research begins.
