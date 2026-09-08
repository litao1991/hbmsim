# HBM standard profiles

The simulator resolves a named HBM profile into one indivisible bundle: organization, address mapping, physical burst, transport bandwidth, command support and timing constraints. `Hbm2Standard` and `Hbm3Standard` are concrete `DramSpec` implementations; they replace the earlier convention where an `HbmStandard` enum and manually edited timing fields could disagree.

| Profile | Pinned Ramulator source | Organization | Timing source |
| --- | --- | --- | --- |
| `hbm2_2000` | `HBM2_2Gb` + `HBM2_2000Mbps` | 2 pseudo-channels, 4 bank groups, 4 banks/group, 16K rows/bank | `python/ramulator/dram/hbm2.py` |
| `hbm3_6400` | `HBM3_16Gb_4hi` + `HBM3_6400Mbps` | 2 pseudo-channels, 4 bank groups, 4 banks/group, 16K rows/bank | `python/ramulator/dram/hbm3.py` |

Both tables are derived from the pinned Ramulator 2.1 revision recorded in `REFERENCE_BASELINES.md`. Speed-bin values remain source-shaped in CK cycles and are resolved once onto HBMSim's integer-picosecond timeline. Detailed constraints cover `tRCDRD`, `tRCDWR`, `tCCDS/L/R`, `tRRDS/L`, `tWTRS/L`, `tWR`, `tRTP`, `tRREFD`, and refresh timing; HBM3 also supplies `tPPD` and half-cycle command-bus occupancy. Standard-owned tables resolve row state into prerequisites (`PREpb/PREab/ACT`) and apply Bank/Channel-scoped transitions for data, refresh, and RFM commands. HBM2/HBM3 separately reserve row and column command buses, allowing legal row/column overlap without introducing a cycle-level loop.

`hbm3_6400` uses the exact transport ratio `32 B / 1250 ps` (25.6 B/ns). It is a permanent HBMSim/Ramulator two-simulator validation profile. The pinned DRAMSys model has no HBM3 standard, so its status is recorded as unsupported rather than filled with synthetic results.
