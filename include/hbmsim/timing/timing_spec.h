#pragma once

#include "hbmsim/common/types.h"
#include "hbmsim/dram/command.h"

#include <cstdint>
#include <vector>

namespace hbmsim {

enum class TimingScope { Bank, BankGroup, PseudoChannel, Channel };

struct TimingConstraint {
  HbmCommand preceding;
  HbmCommand following;
  TimingScope scope;
  std::uint32_t distance = 1;
  SimTime delay = 0;
};

struct HbmTimingSpec {
  SimTime t_rcd = 14'000;
  SimTime t_rp = 14'000;
  SimTime t_cl = 14'000;
  SimTime t_ras = 33'000;
  SimTime t_rc = 47'000;
  SimTime t_ccd = 4'000;
  SimTime t_rrd = 4'000;
  SimTime t_faw = 16'000;
  SimTime t_wtr = 7'500;
  SimTime t_rtw = 7'500;
  SimTime t_rfc = 260'000;
  SimTime t_rfcpb = 160'000;
  SimTime t_rfmab = 260'000;
  SimTime t_rfmpb = 160'000;

  [[nodiscard]] std::vector<TimingConstraint> constraints() const;
  [[nodiscard]] static HbmTimingSpec hbm4_8000();
};

}  // namespace hbmsim
