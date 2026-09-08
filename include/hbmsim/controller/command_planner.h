#pragma once

#include "hbmsim/dram/command.h"
#include "hbmsim/dram/hbm_standard.h"
#include "hbmsim/media/bank_state.h"

#include <cstdint>

namespace hbmsim {

enum class RowPolicy { Open, Closed };

class HbmCommandPlanner {
 public:
  explicit HbmCommandPlanner(const HbmStandard* standard = nullptr)
      : standard_(standard) {}
  [[nodiscard]] HbmCommand next(HbmOp op, std::uint32_t row,
                                const HbmBankState& bank,
                                bool auto_precharge = false) const;

 private:
  const HbmStandard* standard_ = nullptr;
};

}  // namespace hbmsim
