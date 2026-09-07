#pragma once

#include "hbmsim/dram/command.h"
#include "hbmsim/timing/timing_spec.h"

#include <memory>

namespace hbmsim {

enum class RefreshPolicy { AllBank, PerBank };

// Refresh policy is a controller dependency, not a branch spread across the
// event loop.  Standards can add policy-specific deferral or credit handling
// behind this interface without changing transaction behavior.
class IRefreshManager {
 public:
  virtual ~IRefreshManager() = default;
  [[nodiscard]] virtual bool uses_all_bank_refresh() const noexcept = 0;
  [[nodiscard]] virtual HbmCommand all_bank_command() const noexcept = 0;
  [[nodiscard]] virtual HbmCommand per_bank_command() const noexcept = 0;
  [[nodiscard]] virtual SimTime duration(const HbmTimingSpec& spec) const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IRefreshManager> make_refresh_manager(
    RefreshPolicy policy);
[[nodiscard]] SimTime refresh_duration(RefreshPolicy policy,
                                       const HbmTimingSpec& spec) noexcept;

}  // namespace hbmsim
