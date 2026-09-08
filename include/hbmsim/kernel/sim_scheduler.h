#pragma once

#include "hbmsim/common/types.h"

#include <cstdint>
#include <functional>

namespace hbmsim {

using EventToken = std::uint64_t;
using EventCallback = std::function<void()>;

// Component-neutral system-time authority. Implementations assign the global
// same-time order and must outlive every attached HbmSystem.
class ISimScheduler {
 public:
  virtual ~ISimScheduler() = default;
  [[nodiscard]] virtual SimTime now() const noexcept = 0;
  virtual EventToken schedule_at(SimTime when, EventCallback callback) = 0;
  virtual bool cancel(EventToken token) = 0;
};

}  // namespace hbmsim
