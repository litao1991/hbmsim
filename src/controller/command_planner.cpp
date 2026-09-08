#include "hbmsim/controller/command_planner.h"

#include <stdexcept>

namespace hbmsim {

HbmCommand HbmCommandPlanner::next(HbmOp op, std::uint32_t row,
                                    const HbmBankState& bank,
                                    bool auto_precharge) const {
  const auto requested = op == HbmOp::Read
                             ? (auto_precharge ? HbmCommand::ReadAuto
                                               : HbmCommand::Read)
                             : (auto_precharge ? HbmCommand::WriteAuto
                                               : HbmCommand::Write);
  if (standard_ == nullptr) {
    if (bank.precharge_pending ||
        (bank.open_row.has_value() && *bank.open_row != row)) {
      return HbmCommand::PreBank;
    }
    if (!bank.open_row.has_value()) return HbmCommand::Act;
    return requested;
  }
  return next(requested, row, bank, bank.open_row.has_value()).command;
}

CommandDecision HbmCommandPlanner::next(
    HbmCommand requested, std::uint32_t row,
    const HbmBankState& target_bank, bool any_bank_open) const {
  if (standard_ == nullptr) {
    if (requested == HbmCommand::RefreshAllBank ||
        requested == HbmCommand::RfmAllBank) {
      return any_bank_open
                 ? CommandDecision{HbmCommand::PreAll,
                                   CommandScope::Channel, false}
                 : CommandDecision{requested, CommandScope::Channel, true};
    }
    if (requested == HbmCommand::RefreshPerBank ||
        requested == HbmCommand::RfmPerBank) {
      return target_bank.open_row.has_value()
                 ? CommandDecision{HbmCommand::PreBank,
                                   CommandScope::Bank, false}
                 : CommandDecision{requested, CommandScope::Bank, true};
    }
    if (target_bank.precharge_pending ||
        (target_bank.open_row.has_value() &&
         *target_bank.open_row != row)) {
      return {HbmCommand::PreBank, CommandScope::Bank, false};
    }
    if (!target_bank.open_row.has_value() &&
        (requested == HbmCommand::Read || requested == HbmCommand::Write ||
         requested == HbmCommand::ReadAuto ||
         requested == HbmCommand::WriteAuto)) {
      return {HbmCommand::Act, CommandScope::Bank, false};
    }
    return {requested, CommandScope::Bank, true};
  }
  return standard_->resolve_prerequisite(requested, row, target_bank,
                                         any_bank_open);
}

}  // namespace hbmsim
