#pragma once

#include "hbmsim/common/types.h"

#include <cstdint>
#include <optional>

namespace hbmsim {

struct HbmBankState {
  std::optional<std::uint32_t> open_row;
  bool precharge_pending = false;
};

}  // namespace hbmsim
