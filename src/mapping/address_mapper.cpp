#include "hbmsim/mapping/address_mapper.h"

#include <stdexcept>

namespace hbmsim {

HbmAddressMapper::HbmAddressMapper(HbmTopology topology,
                                   std::uint64_t interleave_bytes,
                                   std::uint32_t columns_per_row,
                                   std::uint32_t rows_per_bank,
                                   AddressMapping mapping)
    : topology_(topology),
      interleave_bytes_(interleave_bytes),
      columns_per_row_(columns_per_row),
      rows_per_bank_(rows_per_bank),
      mapping_(mapping) {
  topology_.validate();
  if (interleave_bytes_ == 0 || columns_per_row_ == 0 || rows_per_bank_ == 0) {
    throw std::invalid_argument("HBM address mapping dimensions must be non-zero");
  }
}

HbmAddress HbmAddressMapper::map(std::uint64_t address) const {
  HbmAddress result;
  if (mapping_ == AddressMapping::Hbm2PseudoChannelBrc) {
    // Matches DRAMSys's public `am_hbm2_*_pc_brc.json` bit layout and the
    // corresponding hierarchical HBM2 vector used by Ramulator 2.1.  The
    // SID bit (10) has no HBMSim hierarchy equivalent yet and is ignored.
    result.pseudo_channel = static_cast<std::uint32_t>((address >> 5) & 0x1);
    result.bank_group = static_cast<std::uint32_t>((address >> 6) & 0x3);
    result.bank = static_cast<std::uint32_t>((address >> 8) & 0x3);
    result.column = static_cast<std::uint32_t>((address >> 11) & 0x1f);
    result.row = static_cast<std::uint32_t>((address >> 16) & 0x3fff);
  } else {
    auto line = address / interleave_bytes_;
    const auto global_channel =
        static_cast<std::uint32_t>(line % topology_.channel_count());
    line /= topology_.channel_count();
    result.stack = global_channel / topology_.channels_per_stack;
    result.channel = global_channel % topology_.channels_per_stack;
    result.flat_channel = global_channel;
    result.column = static_cast<std::uint32_t>(line % columns_per_row_);
    line /= columns_per_row_;
    result.row = static_cast<std::uint32_t>(line % rows_per_bank_);
    line /= rows_per_bank_;
    result.bank = static_cast<std::uint32_t>(line % topology_.banks_per_bank_group);
    line /= topology_.banks_per_bank_group;
    result.bank_group =
        static_cast<std::uint32_t>(line % topology_.bank_groups_per_pseudo_channel);
    line /= topology_.bank_groups_per_pseudo_channel;
    result.pseudo_channel =
        static_cast<std::uint32_t>(line % topology_.pseudo_channels_per_channel);
  }
  if (result.pseudo_channel >= topology_.pseudo_channels_per_channel ||
      result.bank_group >= topology_.bank_groups_per_pseudo_channel ||
      result.bank >= topology_.banks_per_bank_group || result.column >= columns_per_row_ ||
      result.row >= rows_per_bank_) {
    throw std::out_of_range("address mapping field is outside topology");
  }

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

std::uint64_t HbmAddressMapper::next_mapping_boundary(std::uint64_t address) const {
  const auto remainder = address % interleave_bytes_;
  return interleave_bytes_ - remainder;
}

HbmAddress HbmAddressMapper::bank_address(std::uint32_t flat_bank) const {
  if (flat_bank >= bank_count()) throw std::out_of_range("flat bank is outside topology");
  HbmAddress result;
  result.flat_bank = flat_bank;
  result.bank = flat_bank % topology_.banks_per_bank_group;
  result.flat_bank_group = flat_bank / topology_.banks_per_bank_group;
  result.bank_group = result.flat_bank_group % topology_.bank_groups_per_pseudo_channel;
  result.flat_pseudo_channel = result.flat_bank_group /
                               topology_.bank_groups_per_pseudo_channel;
  result.pseudo_channel = result.flat_pseudo_channel % topology_.pseudo_channels_per_channel;
  result.flat_channel = result.flat_pseudo_channel / topology_.pseudo_channels_per_channel;
  result.stack = result.flat_channel / topology_.channels_per_stack;
  result.channel = result.flat_channel % topology_.channels_per_stack;
  return result;
}

}  // namespace hbmsim
