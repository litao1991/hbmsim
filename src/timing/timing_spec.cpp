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
  };
}

}  // namespace hbmsim
