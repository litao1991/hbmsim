#pragma once

#include "hbmsim/controller/command_planner.h"
#include "hbmsim/controller/scheduler.h"
#include "hbmsim/mapping/address_mapper.h"
#include "hbmsim/media/bank_state.h"
#include "hbmsim/timing/timing_engine.h"
#include "hbmsim/transaction.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace hbmsim {

// Queue-owned request state.  It deliberately remains an HBM value type while
// the public transaction interface stays standard-neutral.
struct HbmAccess {
  TransactionId parent_id = 0;
  HbmOp op = HbmOp::Read;
  HbmAddress address{};
  std::uint64_t size_bytes = 0;
  SimTime arrival_time = 0;
  ClientId client = 0;
  std::uint64_t sequence = 0;
  HbmAccessClass access_class = HbmAccessClass::RowClosed;
  bool activated = false;
};

struct HbmControllerChannelState {
  std::vector<SimTime> data_bus_ready_at;
  SimTime refresh_busy_until = 0;
  std::deque<HbmAccess> read_queue;
  std::deque<HbmAccess> write_queue;
  std::optional<SimTime> wakeup_at;
  std::optional<SimTime> refresh_due_at;
  bool draining_writes = false;
  bool refresh_pending = false;
  std::uint64_t outstanding_accesses = 0;
  std::uint32_t next_per_bank_refresh = 0;
  std::optional<SimTime> next_refresh_due;
};

struct HbmControllerCandidate {
  bool is_write = false;
  std::size_t index = 0;
  HbmCommand command = HbmCommand::Act;
  SimTime ready_at = 0;
  int priority = 0;
};

// Owns command selection only.  The event-driven system remains responsible
// for scheduling issued commands and delivering external completions.
class HbmController {
 public:
  HbmController(const HbmCommandPlanner& command_planner,
                const HbmTimingEngine& timing_engine,
                const IScheduler& scheduler)
      : command_planner_(command_planner),
        timing_engine_(timing_engine),
        scheduler_(scheduler) {}

  void update_write_drain(HbmControllerChannelState& state,
                          std::size_t high_watermark,
                          std::size_t low_watermark) const;
  [[nodiscard]] std::optional<HbmControllerCandidate> choose_next(
      const HbmControllerChannelState& state,
      const std::vector<HbmBankState>& banks, SimTime now) const;

 private:
  const HbmCommandPlanner& command_planner_;
  const HbmTimingEngine& timing_engine_;
  const IScheduler& scheduler_;
};

}  // namespace hbmsim
