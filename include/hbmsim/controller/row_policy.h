#pragma once

#include "hbmsim/controller/command_planner.h"

#include <memory>

namespace hbmsim {

class IRowPolicy {
 public:
  virtual ~IRowPolicy() = default;
  [[nodiscard]] virtual bool close_after_data_command() const = 0;
};

[[nodiscard]] std::unique_ptr<IRowPolicy> make_row_policy(RowPolicy policy);

}  // namespace hbmsim
