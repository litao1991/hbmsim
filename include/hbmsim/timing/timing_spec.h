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
  // Legacy compact profile.  Retained so existing callers keep their exact
  // behaviour until they opt into a canonical HBM standard profile.
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

  // Canonical HBM2/HBM3 timing vocabulary.  Values are picoseconds and map
  // one-for-one to the relevant speed-bin parameters.  `t_bl` is the data
  // burst duration used to form write-to-read and write-to-precharge delays.
  bool use_extended_hbm_timing = false;
  SimTime t_rcd_rd = 0;
  SimTime t_rcd_wr = 0;
  SimTime t_ccd_s = 0;
  SimTime t_ccd_l = 0;
  SimTime t_rrd_s = 0;
  SimTime t_rrd_l = 0;
  SimTime t_wtr_s = 0;
  SimTime t_wtr_l = 0;
  SimTime t_wr = 0;
  SimTime t_rtp = 0;
  SimTime t_cwl = 0;
  SimTime t_bl = 0;
  SimTime t_ppd = 0;

  [[nodiscard]] std::vector<TimingConstraint> constraints() const;
  [[nodiscard]] static HbmTimingSpec hbm4_8000();
};

}  // namespace hbmsim
