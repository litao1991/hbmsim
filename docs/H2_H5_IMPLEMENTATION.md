# H2–H5 implementation record

## H2 — command-aware timing

`HbmCommandPlanner` emits `ACT`, `PRE`, `RD`, or `WR` from a request's target row and its Bank state. `HbmTimingEngine` stores only the four most recent occurrences of each command at each scoped resource, then derives the next legal issue time from declarative constraints. It does not run a clock loop.

The current constraint set covers `tRCD`, `tRP`, `tRAS`, `tRC`, `tCCD`, `tRRD`, `tFAW`, `tWTR`, `tRTW`, and post-refresh `tRFC`. Resource keys cover Bank, BankGroup, PseudoChannel, and Channel. CAS latency and channel data-bus reservation determine completion after an RD/WR command.

## H3 — controller policy

Each Channel owns independent read/write queues. The controller wakes only for an arrival, a next-legal command, refresh completion, or a transaction completion. It uses FR-FCFS in the active queue: a ready row hit outranks activation/precharge work, then older request sequence wins ties. Configurable write-drain watermarks switch between queues; Open and Closed row policies remain separate from scheduler selection.

## H4 — refresh and extended timing

An optional per-channel refresh manager schedules all-bank or rotating per-bank refresh after a configured interval. Standard-owned prerequisite tables first issue `PREab`/`PREpb` when required, then the maintenance command closes the affected Bank scope and blocks it for its refresh duration. HBM3/HBM4 also support threshold-triggered per-bank RFM through the same command path. To avoid an impossible refresh storm, `refresh_interval` must be at least its selected refresh duration.

## H5 — scalable transactions

`physical_burst_bytes` describes the real transfer unit; `simulation_access_granularity_bytes` optionally groups contiguous bursts into bounded simulation accesses. A client transaction is completed only after all its accesses complete, while stats expose both client transaction count and modelled-access count. The grouping size must be a physical-burst multiple.

This is a transaction-level performance control, not a claim that all grouped bytes target one real DRAM row. For topology-sensitive experiments, select a grouping size compatible with the configured address interleave, then calibrate against the H6 Ramulator/DRAMSys regression suite.

## Test coverage

`test_timing_engine` checks `ACT→RD` and fourth-ACT-window (`tFAW`) constraints. `test_hbm_system` verifies real command sequences for row hit/closed/conflict, FR-FCFS row-hit priority, write drain, refresh blocking, and 16 KiB→4×4 KiB grouping. `test_event_queue` continues to verify stable event ordering.
