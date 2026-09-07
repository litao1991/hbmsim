#pragma once

#include "hbmsim/dram/address.h"
#include "hbmsim/topology.h"

#include <cstdint>

namespace hbmsim {

enum class AddressMapping { Linear, Hbm2PseudoChannelBrc };

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

  // Compatibility view for the original HBM-specific fast path.  New
  // controllers consume this standard-neutral hierarchy instead.
  [[nodiscard]] Address hierarchical() const {
    return {{AddressLevel::Stack, stack},
            {AddressLevel::Channel, channel},
            {AddressLevel::PseudoChannel, pseudo_channel},
            {AddressLevel::BankGroup, bank_group},
            {AddressLevel::Bank, bank},
            {AddressLevel::Row, row},
            {AddressLevel::Column, column}};
  }
};

// H0/H1 use a stable, channel-interleaved mapping.  Its explicit hierarchy
// makes later swappable RoBaBgCoCh and XOR policies an additive change.
class HbmAddressMapper {
 public:
  HbmAddressMapper(HbmTopology topology, std::uint64_t interleave_bytes,
                   std::uint32_t columns_per_row, std::uint32_t rows_per_bank,
                   AddressMapping mapping = AddressMapping::Linear);

  [[nodiscard]] HbmAddress map(std::uint64_t address) const;
  [[nodiscard]] HbmAddress bank_address(std::uint32_t flat_bank) const;
  [[nodiscard]] std::uint32_t bank_count() const;
  [[nodiscard]] std::uint64_t interleave_bytes() const noexcept {
    return interleave_bytes_;
  }

 private:
  HbmTopology topology_;
  std::uint64_t interleave_bytes_;
  std::uint32_t columns_per_row_;
  std::uint32_t rows_per_bank_;
  AddressMapping mapping_;
};

}  // namespace hbmsim
