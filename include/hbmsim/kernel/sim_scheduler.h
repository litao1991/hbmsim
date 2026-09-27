#pragma once

#include "hbmsim/common/types.h"

#include <cstdint>
#include <functional>

namespace hbmsim {

using EventToken = std::uint64_t;
using EventCallback = std::function<void()>;

// Component-neutral system-time authority. Implementations assign the global
// same-time order and must outlive every attached HbmSystem.
// schedule_at never invokes callbacks inline. Tokens are unique and cancel
// returns false for unknown, cancelled or dispatched events. Cancellation
// releases callback captures immediately. Components may submit from callbacks,
// but must not destroy themselves during a callback or recursively run the DES.
class ISimScheduler {
 public:
  virtual ~ISimScheduler() = default;
  [[nodiscard]] virtual SimTime now() const noexcept = 0;
  virtual EventToken schedule_at(SimTime when, EventCallback callback) = 0;
  virtual bool cancel(EventToken token) = 0;
};

}  // namespace hbmsim
