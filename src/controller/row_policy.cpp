#include "hbmsim/controller/row_policy.h"

#include <stdexcept>

namespace hbmsim {
namespace {

class OpenPagePolicy final : public IRowPolicy {
 public:
  [[nodiscard]] bool close_after_data_command() const override { return false; }
};

class ClosedPagePolicy final : public IRowPolicy {
 public:
  [[nodiscard]] bool close_after_data_command() const override { return true; }
};

}  // namespace

std::unique_ptr<IRowPolicy> make_row_policy(RowPolicy policy) {
  switch (policy) {
    case RowPolicy::Open: return std::make_unique<OpenPagePolicy>();
    case RowPolicy::Closed: return std::make_unique<ClosedPagePolicy>();
  }
  throw std::logic_error("unknown row policy");
}

}  // namespace hbmsim
