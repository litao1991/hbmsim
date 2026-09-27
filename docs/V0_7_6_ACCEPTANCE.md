# v0.7.4–v0.7.6 implementation and acceptance

## Scope

- v0.7.4: lazy access expansion, bounded active parents, capacity notifications,
  reentrant completion/ID reuse, immediate event cancellation, time conversion
  checks, and assertions enabled in Debug **and** Release tests.
- v0.7.5: immutable standard-backed DeviceSpec, explicit custom overrides,
  LogicalAccess / controller execution state / PhysicalAccess / LogicalCompletion,
  physical traffic counters, safer read coalescing and effective diagnostics flag.
- v0.7.6: HBFSim v0.8.5 implements actual HBF read -> bounded staging -> HBM
  write -> residency notification. The prior v0.7.3 test covered a manually
  submitted fill only, not a real HBF media read.

## Admission contract

`try_submit_now` accepts arrival <= scheduler.now(); arrival is the original
request time and is never overwritten on a retry. Future arrivals are rejected.
Accepted parents retain a constant-size expansion cursor, not a vector containing
the entire transaction. Children enter finite controller queues as slots free.
There are at most `max_active_transactions` parents (default 1024). Capacity 0
still means unlimited controller queue capacity for baseline compatibility.

Backpressure means **not accepted** and the caller retains ownership. An accepted
large request progresses without external resubmission. Slot credits are signalled
on issue and active-parent credit on completion. Notifications are hints: callers
must retry admission, not assume a reservation. Rejected counters count attempts.
Queue wait includes time before child admission, preserving end-to-end latency.

The CLI checks monotonicity against the previous trace arrival, not current time.
Historical same-time CLI event-drain order is preserved. Scheduler implementations
must defer callbacks (no inline execution), use stable FIFO same-time ordering,
and outlive attached components. Callbacks can submit and replace callbacks but
must not recursively run the scheduler or destroy the currently executing component.

## Configuration, accesses and statistics

`DeviceSpec::from_standard` reads organization/timing directly from the immutable
standard. To override, first use `as_custom()`; this deliberately leaves standard
command behavior and selects the existing custom-model path. It is not a modified
JEDEC speed bin. No second mutable copy of profile organization/timing is exposed.

`LogicalAccess` contains requester identity and address. `HbmAccess` adds private
controller progression. `PhysicalAccess` owns one issued transfer and its logical
completion fanout, dispatched in one DES event. `modeled_accesses` counts logical
children; `physical_accesses` and physical byte counters count bus transfers.
Read merging requires identical byte address/size and ordering domain, and is
blocked by a queued overlapping write. Ordering-domain metadata does not claim
full memory-consistency enforcement or ordered completion across all requests.

`detailed_stats=false` omits per-bank/PC vectors and latency-stage aggregation /
completion breakdowns. Aggregate counts, queue depths and data bus accounting
remain available. Default reporting is unchanged.

## Acceptance coverage

Local validation on 2026-09-27 (AppleClang 21, macOS):

| Suite | Debug | Release | ASan + UBSan |
| --- | --- | --- | --- |
| HBMSim | 7/7 | 7/7 | 7/7 |
| HBFSim with HBMSim integration | 38/38 | 38/38 | 38/38 |

These are local results, not results from a newly published GitHub commit.

- HBMSim: Debug/Release and ASan+UBSan, including `h7_boundary` and actual CLI
  bounded-queue replay. Test assertions are explicitly retained in Release.
- HBFSim: full suite with integration enabled, plus Debug/Release/sanitizer
  integration CI matrix. Tests cover real 64 B / 4 KiB / 16 KiB reads, capacity-one
  HBM queue, two staging slots, capacity callback retries, fractional ps entry,
  read errors, partial multi-command failure, request-limit exhaustion and teardown.
- HBMSim streaming test reuses one ID for 2000 callback-driven transactions and
  verifies active parents and completion history are empty after draining.
- HBM2/HBM3 semantic gates and frozen HBM2 v0.6.4 numerical gate run locally.
  All completion and summary CSVs for 14 HBM2/HBM3 microtraces also match the
  pre-change local executable byte-for-byte.
  External Ramulator/DRAMSys executables are **not rebuilt or rerun** by this
  acceptance step; their existing GitHub Actions H6 workflow remains in place.

## Deliberately not included

Residency cache lookup/eviction, dirty writeback, speculative prefetch, compute
dependency scheduling, automatic HBF ECC retries, extra DMA/fabric resource
models, DDR4 and complete HBM4 are later work. Residency here is a completion
notification, not an unbounded resident-address history.

Publishing requires both repositories: publish HBMSim first, then HBFSim, and
pin HBFSim's HBMSim checkout to the resulting tested commit. This local change
does not invent a commit SHA or claim a new GitHub Actions run has succeeded.
