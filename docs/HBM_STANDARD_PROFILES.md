# HBM standard profiles

The simulator resolves a named HBM profile into one indivisible bundle: organization, address mapping, physical burst, transport bandwidth, command support and timing constraints. `Hbm2Standard` and `Hbm3Standard` are concrete `DramSpec` implementations; they replace the earlier convention where an `HbmStandard` enum and manually edited timing fields could disagree.

| Profile | Pinned Ramulator source | Organization | Timing source |
| --- | --- | --- | --- |
| `hbm2_2000` | `HBM2_2Gb` + `HBM2_2000Mbps` | 2 pseudo-channels, 4 bank groups, 4 banks/group, 16K rows/bank | `python/ramulator/dram/hbm2.py` |
| `hbm3_6400` | `HBM3_16Gb_4hi` + `HBM3_6400Mbps` | 2 pseudo-channels, 4 bank groups, 4 banks/group, 16K rows/bank | `python/ramulator/dram/hbm3.py` |

Both tables are derived from the pinned Ramulator 2.1 revision recorded in `REFERENCE_BASELINES.md`. Values remain in picoseconds. Detailed constraints cover `tRCDRD`, `tRCDWR`, `tCCDS/L`, `tRRDS/L`, `tWTRS/L`, `tWR`, and `tRTP`; HBM3 also supplies `tPPD`.

`hbm3_6400` is a usable simulator profile, but it is not yet a cross-simulator numerical baseline. Its pseudo-channel transport is conservatively represented as 25 B/ns because the current transaction transport uses integral bytes/ns; the exact 25.6 B/ns representation belongs to the v0.6 resource/transport refinement.
