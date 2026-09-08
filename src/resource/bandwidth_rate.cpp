#include "hbmsim/resource/bandwidth_rate.h"

#include <limits>
#include <stdexcept>

namespace hbmsim {

void BandwidthRate::validate() const {
  if (bytes == 0 || interval == 0) {
    throw std::invalid_argument("bandwidth rate numerator and interval must be non-zero");
  }
}

SimTime BandwidthRate::transfer_time(std::uint64_t transfer_bytes) const {
  validate();
  constexpr auto max_time = std::numeric_limits<SimTime>::max();
  if (transfer_bytes > max_time / interval) {
    throw std::overflow_error("transfer time overflows SimTime");
  }
  const auto scaled = transfer_bytes * interval;
  if (scaled > max_time - (bytes - 1)) {
    throw std::overflow_error("transfer time overflows SimTime");
  }
  return (scaled + bytes - 1) / bytes;
}

}  // namespace hbmsim
