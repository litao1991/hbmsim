#pragma once

#include "hbmsim/common/types.h"

#include <cstdint>
#include <optional>

namespace hbmsim {

struct HbmBankState {
  std::optional<std::uint32_t> open_row;
  bool precharge_pending = false;
  bool refresh_pending = false;
  bool rfm_pending = false;
  std::uint32_t activation_count = 0;
};

}  // namespace hbmsim
