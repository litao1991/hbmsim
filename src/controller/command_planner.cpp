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
  if (!standard_->supports(requested)) {
    throw std::logic_error("requested command is not supported by standard");
  }
  for (const auto& rule : standard_->prerequisites()) {
    if (rule.requested != requested) continue;
    const bool matches = rule.condition == BankCondition::Closed
                             ? !bank.open_row.has_value()
                             : bank.open_row.has_value() && *bank.open_row != row;
    if (matches) return rule.prerequisite;
  }
  return requested;
}

}  // namespace hbmsim
