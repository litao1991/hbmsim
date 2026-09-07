#include "hbmsim/dram/hbm_standard.h"

namespace hbmsim {
namespace {

bool supports_common_hbm_command(DramCommand command) {
  return command == DramCommand::Activate || command == DramCommand::Precharge ||
         command == DramCommand::Read || command == DramCommand::Write ||
         command == DramCommand::RefreshAllBank ||
         command == DramCommand::RefreshPerBank;
}

}  // namespace

bool Hbm2Standard::supports(DramCommand command) const {
  return supports_common_hbm_command(command);
}

bool Hbm3Standard::supports(DramCommand command) const {
  return supports_common_hbm_command(command) || command == DramCommand::RfmAllBank ||
         command == DramCommand::RfmPerBank;
}

const HbmStandardProfile& Hbm2Standard::profile_2000() {
  // Pinned Ramulator 2.1 source: python/ramulator/dram/hbm2.py,
  // HBM2_2Gb + HBM2_2000Mbps.  Derived nCCDL/nRRD/nFAW values are resolved
  // using that source's documented formulas at tCK=1000 ps.
  static const HbmStandardProfile profile = [] {
    HbmStandardProfile value{};
    value.id = "hbm2_2000";
    value.topology.stacks = 1;
    value.topology.channels_per_stack = 1;
    value.topology.pseudo_channels_per_channel = 2;
    value.topology.bank_groups_per_pseudo_channel = 4;
    value.topology.banks_per_bank_group = 4;
    value.address_mapping = AddressMapping::Hbm2PseudoChannelBrc;
    value.columns_per_row = 32;
    value.rows_per_bank = 16'384;
    value.physical_burst_bytes = 32;
    value.address_interleave_bytes = 32;
    value.pseudo_channel_bandwidth_bytes_per_ns = 16;
    auto& t = value.timing;
    t.use_extended_hbm_timing = true;
    t.t_cl = 14'000;
    t.t_rcd = 14'000;
    t.t_rcd_rd = 14'000;
    t.t_rcd_wr = 12'000;
    t.t_rp = 14'000;
    t.t_ras = 34'000;
    t.t_rc = 48'000;
    t.t_wr = 16'000;
    t.t_rtp = 5'000;
    t.t_cwl = 5'000;
    t.t_bl = 2'000;
    t.t_ccd_s = 2'000;
    t.t_ccd_l = 4'000;
    t.t_rrd_s = 4'000;
    t.t_rrd_l = 4'000;
    t.t_faw = 15'000;
    t.t_wtr_s = 6'000;
    t.t_wtr_l = 8'000;
    t.t_rtw = 15'000;
    t.t_rfc = 260'000;
    t.t_rfcpb = 160'000;
    t.t_rfmab = t.t_rfc;
    t.t_rfmpb = t.t_rfcpb;
    return value;
  }();
  return profile;
}

const HbmStandardProfile& Hbm3Standard::profile_6400() {
  // Pinned Ramulator 2.1 source: python/ramulator/dram/hbm3.py,
  // HBM3_16Gb_4hi + HBM3_6400Mbps.  This is an unvalidated profile adapter;
  // V0.6 has a separate HBM3 cross-simulator gate.
  static const HbmStandardProfile profile = [] {
    HbmStandardProfile value{};
    value.id = "hbm3_6400";
    value.topology.stacks = 1;
    value.topology.channels_per_stack = 1;
    value.topology.pseudo_channels_per_channel = 2;
    value.topology.bank_groups_per_pseudo_channel = 4;
    value.topology.banks_per_bank_group = 4;
    value.address_mapping = AddressMapping::Linear;
    value.columns_per_row = 32;
    value.rows_per_bank = 16'384;
    value.physical_burst_bytes = 32;
    value.address_interleave_bytes = 32;
    // 32-bit pseudo-channel at 6.4 GT/s is 25.6 B/ns.  The current integer
    // bandwidth transport uses the conservative 25 B/ns approximation.
    value.pseudo_channel_bandwidth_bytes_per_ns = 25;
    auto& t = value.timing;
    t.use_extended_hbm_timing = true;
    t.t_cl = 12'500;
    t.t_rcd = 19'375;
    t.t_rcd_rd = 19'375;
    t.t_rcd_wr = 9'375;
    t.t_rp = 16'250;
    t.t_ras = 28'125;
    t.t_rc = 44'375;
    t.t_wr = 20'625;
    t.t_rtp = 5'625;
    t.t_cwl = 6'250;
    t.t_bl = 1'250;
    t.t_ccd_s = 1'250;
    t.t_ccd_l = 2'500;
    t.t_rrd_s = 2'500;
    t.t_rrd_l = 3'125;
    t.t_faw = 15'000;
    t.t_wtr_s = 4'375;
    t.t_wtr_l = 6'250;
    t.t_rtw = 10'625;
    t.t_ppd = 1'250;
    t.t_rfc = 260'000;
    t.t_rfcpb = 200'000;
    t.t_rfmab = t.t_rfc;
    t.t_rfmpb = t.t_rfcpb;
    return value;
  }();
  return profile;
}

}  // namespace hbmsim
