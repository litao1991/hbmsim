#include "hbmsim/timing/timing_spec.h"

namespace hbmsim {

std::vector<TimingConstraint> HbmTimingSpec::constraints() const {
  if (use_extended_hbm_timing) {
    return {
        {HbmCommand::Act, HbmCommand::Read, TimingScope::Bank, 1, t_rcd_rd},
        {HbmCommand::Act, HbmCommand::Write, TimingScope::Bank, 1, t_rcd_wr},
        {HbmCommand::Pre, HbmCommand::Act, TimingScope::Bank, 1, t_rp},
        {HbmCommand::Act, HbmCommand::Pre, TimingScope::Bank, 1, t_ras},
        {HbmCommand::Act, HbmCommand::Act, TimingScope::Bank, 1, t_rc},
        {HbmCommand::Read, HbmCommand::Pre, TimingScope::Bank, 1, t_rtp},
        {HbmCommand::Write, HbmCommand::Pre, TimingScope::Bank, 1,
         t_cwl + t_bl + t_wr},
        {HbmCommand::Read, HbmCommand::Read, TimingScope::PseudoChannel, 1, t_ccd_s},
        {HbmCommand::Write, HbmCommand::Write, TimingScope::PseudoChannel, 1, t_ccd_s},
        {HbmCommand::Read, HbmCommand::Write, TimingScope::PseudoChannel, 1, t_rtw},
        {HbmCommand::Write, HbmCommand::Read, TimingScope::PseudoChannel, 1,
         t_cwl + t_bl + t_wtr_s},
        {HbmCommand::Read, HbmCommand::Read, TimingScope::BankGroup, 1, t_ccd_l},
        {HbmCommand::Write, HbmCommand::Write, TimingScope::BankGroup, 1, t_ccd_l},
        {HbmCommand::Write, HbmCommand::Read, TimingScope::BankGroup, 1,
         t_cwl + t_bl + t_wtr_l},
        {HbmCommand::Read, HbmCommand::Read, TimingScope::Sid, 1, t_ccd_s},
        {HbmCommand::Write, HbmCommand::Write, TimingScope::Sid, 1, t_ccd_s},
        {HbmCommand::Act, HbmCommand::Act, TimingScope::BankGroup, 1, t_rrd_l},
        {HbmCommand::Act, HbmCommand::Act, TimingScope::PseudoChannel, 1, t_rrd_s},
        {HbmCommand::Act, HbmCommand::Act, TimingScope::PseudoChannel, 4, t_faw},
        {HbmCommand::Pre, HbmCommand::Pre, TimingScope::PseudoChannel, 1, t_ppd},
        {HbmCommand::RefreshAllBank, HbmCommand::Act, TimingScope::Channel, 1, t_rfc},
        {HbmCommand::RefreshPerBank, HbmCommand::Act, TimingScope::Bank, 1, t_rfcpb},
        {HbmCommand::RefreshPerBank, HbmCommand::Act,
         TimingScope::PseudoChannel, 1, t_rrefd},
        {HbmCommand::RefreshPerBank, HbmCommand::RefreshPerBank,
         TimingScope::PseudoChannel, 1, t_rrefd},
        {HbmCommand::RfmAllBank, HbmCommand::Act, TimingScope::Channel, 1, t_rfmab},
        {HbmCommand::RfmPerBank, HbmCommand::Act, TimingScope::Bank, 1, t_rfmpb},
        {HbmCommand::Act, HbmCommand::RefreshPerBank, TimingScope::Bank, 1, t_rc},
        {HbmCommand::Pre, HbmCommand::RefreshPerBank, TimingScope::Bank, 1, t_rp},
        {HbmCommand::Act, HbmCommand::RefreshAllBank,
         TimingScope::PseudoChannel, 1, t_rc},
        {HbmCommand::Pre, HbmCommand::RefreshAllBank,
         TimingScope::PseudoChannel, 1, t_rp},
        {HbmCommand::RefreshPerBank, HbmCommand::RefreshAllBank,
         TimingScope::PseudoChannel, 1, t_rfcpb},
        {HbmCommand::Act, HbmCommand::RfmPerBank, TimingScope::Bank, 1, t_rc},
    };
  }
  return {
      {HbmCommand::Act, HbmCommand::Read, TimingScope::Bank, 1, t_rcd},
      {HbmCommand::Act, HbmCommand::Write, TimingScope::Bank, 1, t_rcd},
      {HbmCommand::Pre, HbmCommand::Act, TimingScope::Bank, 1, t_rp},
      {HbmCommand::Act, HbmCommand::Pre, TimingScope::Bank, 1, t_ras},
      {HbmCommand::Act, HbmCommand::Act, TimingScope::Bank, 1, t_rc},
      {HbmCommand::Read, HbmCommand::Read, TimingScope::PseudoChannel, 1, t_ccd},
      {HbmCommand::Write, HbmCommand::Write, TimingScope::PseudoChannel, 1, t_ccd},
      {HbmCommand::Read, HbmCommand::Write, TimingScope::PseudoChannel, 1, t_rtw},
      {HbmCommand::Write, HbmCommand::Read, TimingScope::PseudoChannel, 1, t_wtr},
      {HbmCommand::Act, HbmCommand::Act, TimingScope::BankGroup, 1, t_rrd},
      {HbmCommand::Act, HbmCommand::Act, TimingScope::PseudoChannel, 1, t_rrd},
      {HbmCommand::Act, HbmCommand::Act, TimingScope::PseudoChannel, 4, t_faw},
      {HbmCommand::RefreshAllBank, HbmCommand::Act, TimingScope::Channel, 1, t_rfc},
      {HbmCommand::RefreshPerBank, HbmCommand::Act, TimingScope::Bank, 1, t_rfcpb},
      {HbmCommand::RfmAllBank, HbmCommand::Act, TimingScope::Channel, 1, t_rfmab},
      {HbmCommand::RfmPerBank, HbmCommand::Act, TimingScope::Bank, 1, t_rfmpb},
      {HbmCommand::Act, HbmCommand::RefreshPerBank, TimingScope::Bank, 1, t_rc},
      {HbmCommand::Act, HbmCommand::RfmPerBank, TimingScope::Bank, 1, t_rc},
  };
}

HbmTimingSpec HbmTimingSpec::hbm4_8000() {
  HbmTimingSpec spec;
  spec.t_command = 500;
  spec.t_rcd = 39 * 500;
  spec.t_rp = 33 * 500;
  spec.t_cl = 20 * 500;
  spec.t_ras = 57 * 500;
  spec.t_rc = spec.t_ras + spec.t_rp;
  spec.t_ccd = 2 * 500;
  spec.t_rrd = 5 * 500;
  spec.t_faw = 30 * 500;
  spec.t_wtr = (10 + 2 + 9) * 500;
  spec.t_rtw = 15 * 500;
  spec.t_rfc = 450'000;
  spec.t_rfcpb = 280'000;
  spec.t_rfmab = spec.t_rfc;
  spec.t_rfmpb = spec.t_rfcpb;
  return spec;
}

}  // namespace hbmsim
