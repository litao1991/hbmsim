#pragma once

#include "hbmsim/dram/spec.h"

#include <array>

namespace hbmsim {

class HbmDramSpec final : public DramSpec {
 public:
  [[nodiscard]] std::string_view name() const override { return "HBM"; }
  [[nodiscard]] std::span<const AddressLevel> levels() const override { return kLevels; }
  [[nodiscard]] bool supports(DramCommand command) const override {
    return command == DramCommand::Activate || command == DramCommand::Precharge ||
           command == DramCommand::Read || command == DramCommand::Write ||
           command == DramCommand::RefreshAllBank || command == DramCommand::RefreshPerBank ||
           command == DramCommand::RfmAllBank || command == DramCommand::RfmPerBank;
  }

 private:
  inline static constexpr std::array<AddressLevel, 7> kLevels{
      AddressLevel::Stack, AddressLevel::Channel, AddressLevel::PseudoChannel,
      AddressLevel::BankGroup, AddressLevel::Bank, AddressLevel::Row, AddressLevel::Column};
};

}  // namespace hbmsim
