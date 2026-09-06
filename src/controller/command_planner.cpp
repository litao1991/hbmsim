#include "hbmsim/controller/command_planner.h"

namespace hbmsim {

HbmCommand HbmCommandPlanner::next(HbmOp op, std::uint32_t row,
                                    const HbmBankState& bank) const {
  if (bank.precharge_pending ||
      (bank.open_row.has_value() && *bank.open_row != row)) return HbmCommand::Pre;
  if (!bank.open_row.has_value()) return HbmCommand::Act;
  return op == HbmOp::Read ? HbmCommand::Read : HbmCommand::Write;
}

}  // namespace hbmsim
