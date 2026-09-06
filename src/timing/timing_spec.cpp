#include "hbmsim/timing/timing_spec.h"

namespace hbmsim {

std::vector<TimingConstraint> HbmTimingSpec::constraints() const {
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
