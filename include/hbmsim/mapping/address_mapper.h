#pragma once

#include "hbmsim/topology.h"

#include <cstdint>

namespace hbmsim {

struct HbmAddress {
  std::uint32_t stack = 0;
  std::uint32_t channel = 0;
  std::uint32_t pseudo_channel = 0;
  std::uint32_t bank_group = 0;
  std::uint32_t bank = 0;
  std::uint32_t row = 0;
  std::uint32_t column = 0;
  std::uint32_t flat_channel = 0;
  std::uint32_t flat_pseudo_channel = 0;
  std::uint32_t flat_bank_group = 0;
  std::uint32_t flat_bank = 0;
};

// H0/H1 use a stable, channel-interleaved mapping.  Its explicit hierarchy
// makes later swappable RoBaBgCoCh and XOR policies an additive change.
class HbmAddressMapper {
 public:
  HbmAddressMapper(HbmTopology topology, std::uint64_t interleave_bytes,
                   std::uint32_t columns_per_row, std::uint32_t rows_per_bank);

  [[nodiscard]] HbmAddress map(std::uint64_t address) const;
  [[nodiscard]] std::uint32_t bank_count() const;

 private:
  HbmTopology topology_;
  std::uint64_t interleave_bytes_;
  std::uint32_t columns_per_row_;
  std::uint32_t rows_per_bank_;
};

}  // namespace hbmsim
