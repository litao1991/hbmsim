#include "hbmsim/controller/refresh_manager.h"

namespace hbmsim {
namespace {

class AllBankRefreshManager final : public IRefreshManager {
 public:
  [[nodiscard]] bool uses_all_bank_refresh() const noexcept override { return true; }
  [[nodiscard]] HbmCommand all_bank_command() const noexcept override {
    return HbmCommand::RefreshAllBank;
  }
  [[nodiscard]] HbmCommand per_bank_command() const noexcept override {
    return HbmCommand::RefreshPerBank;
  }
  [[nodiscard]] SimTime duration(const HbmTimingSpec& spec) const noexcept override {
    return spec.t_rfc;
  }
};

class PerBankRefreshManager final : public IRefreshManager {
 public:
  [[nodiscard]] bool uses_all_bank_refresh() const noexcept override { return false; }
  [[nodiscard]] HbmCommand all_bank_command() const noexcept override {
    return HbmCommand::RefreshAllBank;
  }
  [[nodiscard]] HbmCommand per_bank_command() const noexcept override {
    return HbmCommand::RefreshPerBank;
  }
  [[nodiscard]] SimTime duration(const HbmTimingSpec& spec) const noexcept override {
    return spec.t_rfcpb;
  }
};

}  // namespace

std::unique_ptr<IRefreshManager> make_refresh_manager(RefreshPolicy policy) {
  switch (policy) {
    case RefreshPolicy::AllBank: return std::make_unique<AllBankRefreshManager>();
    case RefreshPolicy::PerBank: return std::make_unique<PerBankRefreshManager>();
  }
  return std::make_unique<AllBankRefreshManager>();
}

SimTime refresh_duration(RefreshPolicy policy, const HbmTimingSpec& spec) noexcept {
  return policy == RefreshPolicy::AllBank ? spec.t_rfc : spec.t_rfcpb;
}

}  // namespace hbmsim
