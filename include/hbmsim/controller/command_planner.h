#pragma once

#include "hbmsim/dram/command.h"
#include "hbmsim/media/bank_state.h"

#include <cstdint>

namespace hbmsim {

enum class RowPolicy { Open, Closed };

class HbmCommandPlanner {
 public:
  [[nodiscard]] HbmCommand next(HbmOp op, std::uint32_t row,
                                const HbmBankState& bank) const;
};

}  // namespace hbmsim
