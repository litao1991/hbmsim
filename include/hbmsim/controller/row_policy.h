#pragma once

#include "hbmsim/controller/command_planner.h"

#include <memory>

namespace hbmsim {

class IRowPolicy {
 public:
  virtual ~IRowPolicy() = default;
  [[nodiscard]] virtual bool use_auto_precharge() const = 0;
};

[[nodiscard]] std::unique_ptr<IRowPolicy> make_row_policy(RowPolicy policy);

}  // namespace hbmsim
