#pragma once

#include "hbmsim/dram/spec.h"
#include "hbmsim/mapping/address_mapper.h"
#include "hbmsim/timing/timing_spec.h"
#include "hbmsim/topology.h"

#include <array>
#include <string_view>

namespace hbmsim {

// A resolved speed-bin profile: organization, mapping and timing are selected
// together so a controller cannot accidentally combine HBM2 geometry with an
// HBM3 timing table.
struct HbmStandardProfile {
  std::string_view id;
  HbmTopology topology;
  HbmTimingSpec timing;
  AddressMapping address_mapping = AddressMapping::Linear;
  std::uint32_t columns_per_row = 0;
  std::uint32_t rows_per_bank = 0;
  std::uint64_t physical_burst_bytes = 32;
  std::uint64_t address_interleave_bytes = 32;
  std::uint64_t pseudo_channel_bandwidth_bytes_per_ns = 0;
};

class Hbm2Standard final : public DramSpec {
 public:
  [[nodiscard]] std::string_view name() const override { return "HBM2"; }
  [[nodiscard]] std::span<const AddressLevel> levels() const override { return kLevels; }
  [[nodiscard]] bool supports(DramCommand command) const override;
  [[nodiscard]] static const HbmStandardProfile& profile_2000();

 private:
  inline static constexpr std::array<AddressLevel, 7> kLevels{
      AddressLevel::Channel, AddressLevel::PseudoChannel, AddressLevel::Stack,
      AddressLevel::BankGroup, AddressLevel::Bank, AddressLevel::Row,
      AddressLevel::Column};
};

class Hbm3Standard final : public DramSpec {
 public:
  [[nodiscard]] std::string_view name() const override { return "HBM3"; }
  [[nodiscard]] std::span<const AddressLevel> levels() const override { return kLevels; }
  [[nodiscard]] bool supports(DramCommand command) const override;
  [[nodiscard]] static const HbmStandardProfile& profile_6400();

 private:
  inline static constexpr std::array<AddressLevel, 7> kLevels{
      AddressLevel::Channel, AddressLevel::PseudoChannel, AddressLevel::Stack,
      AddressLevel::BankGroup, AddressLevel::Bank, AddressLevel::Row,
      AddressLevel::Column};
};

}  // namespace hbmsim
