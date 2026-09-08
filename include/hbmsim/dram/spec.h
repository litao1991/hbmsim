#pragma once

#include "hbmsim/dram/address.h"

#include <span>
#include <string_view>

namespace hbmsim {

// The common commands deliberately cover only the portable read path.  A
// concrete standard may expose additional commands without extending clients.
enum class DramCommand {
  Activate,
  PrechargeBank,
  PrechargeAll,
  Read,
  Write,
  ReadAuto,
  WriteAuto,
  RefreshAllBank,
  RefreshPerBank,
  RfmAllBank,
  RfmPerBank,
  Precharge = PrechargeBank,
};

class DramSpec {
 public:
  virtual ~DramSpec() = default;
  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual std::span<const AddressLevel> levels() const = 0;
  [[nodiscard]] virtual bool supports(DramCommand command) const = 0;
};

}  // namespace hbmsim
