#pragma once

#include "hbmsim/common/types.h"

#include <cstdint>

namespace hbmsim {

// Exact rational transport rate.  For example HBM3-6400 is represented as
// 32 bytes per 1250 ps, avoiding both floating point and a 25 B/ns rounding.
struct BandwidthRate {
  std::uint64_t bytes = 32;
  SimTime interval = 1'000;

  void validate() const;
  [[nodiscard]] SimTime transfer_time(std::uint64_t transfer_bytes) const;
};

}  // namespace hbmsim
