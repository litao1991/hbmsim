#include "hbmsim/mapping/address_mapper.h"

#include <stdexcept>

namespace hbmsim {

HbmAddressMapper::HbmAddressMapper(HbmTopology topology,
                                   std::uint64_t interleave_bytes,
                                   std::uint32_t columns_per_row,
                                   std::uint32_t rows_per_bank)
    : topology_(topology),
      interleave_bytes_(interleave_bytes),
      columns_per_row_(columns_per_row),
      rows_per_bank_(rows_per_bank) {
  topology_.validate();
  if (interleave_bytes_ == 0 || columns_per_row_ == 0 || rows_per_bank_ == 0) {
    throw std::invalid_argument("HBM address mapping dimensions must be non-zero");
  }
}

HbmAddress HbmAddressMapper::map(std::uint64_t address) const {
  auto line = address / interleave_bytes_;
  const auto global_channel = static_cast<std::uint32_t>(line % topology_.channel_count());
  line /= topology_.channel_count();

  HbmAddress result;
  result.stack = global_channel / topology_.channels_per_stack;
  result.channel = global_channel % topology_.channels_per_stack;
  result.flat_channel = global_channel;
  result.column = static_cast<std::uint32_t>(line % columns_per_row_);
  line /= columns_per_row_;
  result.row = static_cast<std::uint32_t>(line % rows_per_bank_);
  line /= rows_per_bank_;
  result.bank = static_cast<std::uint32_t>(line % topology_.banks_per_bank_group);
  line /= topology_.banks_per_bank_group;
  result.bank_group = static_cast<std::uint32_t>(line % topology_.bank_groups_per_pseudo_channel);
  line /= topology_.bank_groups_per_pseudo_channel;
  result.pseudo_channel = static_cast<std::uint32_t>(line % topology_.pseudo_channels_per_channel);

  result.flat_pseudo_channel =
      result.flat_channel * topology_.pseudo_channels_per_channel + result.pseudo_channel;
  result.flat_bank_group =
      result.flat_pseudo_channel * topology_.bank_groups_per_pseudo_channel + result.bank_group;

  result.flat_bank =
      result.flat_bank_group * topology_.banks_per_bank_group + result.bank;
  return result;
}

std::uint32_t HbmAddressMapper::bank_count() const {
  return topology_.channel_count() * topology_.pseudo_channels_per_channel *
         topology_.bank_groups_per_pseudo_channel * topology_.banks_per_bank_group;
}

}  // namespace hbmsim
