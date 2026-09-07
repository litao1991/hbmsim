#pragma once

#include "hbmsim/common/types.h"
#include "hbmsim/dram/spec.h"

#include <optional>
#include <span>

namespace hbmsim {

enum class SchedulerKind { FrFcfs, FrFcfsRowHit, Fifo };

// Controller-owned state is converted into this value type before policy
// selection, keeping scheduling independent from HBM bank storage.
struct SchedulerCandidate {
  std::size_t queue_index = 0;
  bool is_write = false;
  DramCommand command = DramCommand::Activate;
  SimTime ready_at = 0;
  std::uint64_t sequence = 0;
  bool row_hit = false;
  bool data_command = false;
};

class IScheduler {
 public:
  virtual ~IScheduler() = default;
  [[nodiscard]] virtual std::optional<SchedulerCandidate> choose(
      std::span<const SchedulerCandidate> candidates, SimTime now) const = 0;
};

[[nodiscard]] const IScheduler& scheduler_for(SchedulerKind kind);

}  // namespace hbmsim
