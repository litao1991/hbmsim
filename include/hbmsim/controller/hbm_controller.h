#pragma once

#include "hbmsim/controller/command_planner.h"
#include "hbmsim/controller/refresh_manager.h"
#include "hbmsim/controller/row_policy.h"
#include "hbmsim/controller/scheduler.h"
#include "hbmsim/mapping/address_mapper.h"
#include "hbmsim/media/bank_state.h"
#include "hbmsim/resource/bandwidth_rate.h"
#include "hbmsim/stats.h"
#include "hbmsim/timing/timing_engine.h"
#include "hbmsim/transaction.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace hbmsim {

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
  bool first_command_issued = false;
  bool starvation_reported = false;
  SimTime enqueued_at = 0;
  SimTime first_command_at = 0;
  bool command_eligible = false;
  SimTime command_eligible_since = 0;
  HbmLatencyBreakdown latency_breakdown{};
};

struct HbmControllerConfig {
  std::uint32_t channel = 0;
  std::uint32_t first_flat_bank = 0;
  std::uint32_t bank_count = 0;
  std::uint32_t pseudo_channel_count = 0;
  HbmTimingSpec timing{};
  std::shared_ptr<const HbmStandard> standard;
  BandwidthRate data_rate{};
  SchedulerKind scheduler = SchedulerKind::FrFcfs;
  RowPolicy row_policy = RowPolicy::Open;
  RefreshPolicy refresh_policy = RefreshPolicy::AllBank;
  std::size_t write_drain_high_watermark = 16;
  std::size_t write_drain_low_watermark = 4;
  std::size_t read_queue_capacity = 0;
  std::size_t write_queue_capacity = 0;
  bool enable_request_merging = false;
  SimTime starvation_threshold = 0;
  SimTime refresh_interval = 0;
  bool enable_rfm = false;
  std::uint32_t rfm_activation_threshold = 0;
};

struct HbmIssuedAccess {
  std::vector<HbmAccess> accesses;
  SimTime completion_time = 0;
};

struct HbmControllerStep {
  std::optional<SimTime> wake_at;
  std::optional<HbmIssuedAccess> issued_access;
};

// One stateful controller per physical HBM channel. It owns queues, bank
// state, timing history, refresh state and data/command resources. It never
// advances global time: HbmSystem schedules the returned absolute events.
class HbmController {
 public:
  HbmController(HbmControllerConfig config, const IHbmAddressMapper& mapper,
                HbmStats& stats);

  [[nodiscard]] std::uint32_t channel() const noexcept { return config_.channel; }
  [[nodiscard]] bool has_outstanding() const noexcept {
    return outstanding_accesses_ != 0;
  }
  [[nodiscard]] bool can_reserve(std::size_t reads, std::size_t writes) const;
  void reserve(std::size_t reads, std::size_t writes);
  void cancel_reservation(std::size_t reads, std::size_t writes);
  void admit_reserved(HbmAccess access, SimTime now);

  [[nodiscard]] bool mark_wakeup(SimTime when);
  void clear_wakeup(SimTime when);
  [[nodiscard]] std::optional<SimTime> claim_refresh_event();
  void handle_refresh_event(SimTime now);
  [[nodiscard]] HbmControllerStep drive(SimTime now);
  void complete_access(const HbmAccess& access, SimTime completion_time);

 private:
  struct Candidate {
    bool is_write = false;
    std::size_t index = 0;
    HbmCommand command = HbmCommand::Act;
    SimTime ready_at = 0;
    int priority = 0;
  };
  struct MaintenanceCandidate {
    HbmCommand requested = HbmCommand::RefreshPerBank;
    HbmCommand command = HbmCommand::RefreshPerBank;
    std::uint32_t local_bank = 0;
    SimTime ready_at = 0;
    bool final_command = true;
  };

  [[nodiscard]] std::size_t local_bank(std::uint32_t flat_bank) const;
  [[nodiscard]] std::optional<Candidate> choose_next(SimTime now);
  [[nodiscard]] std::optional<MaintenanceCandidate> choose_maintenance(
      SimTime now) const;
  [[nodiscard]] SimTime earliest_all_bank(HbmCommand command,
                                          SimTime now) const;
  [[nodiscard]] SimTime earliest_command(HbmCommand command,
                                         const HbmAddress& address,
                                         SimTime now) const;
  [[nodiscard]] SimTime command_bus_ready(HbmCommand command) const;
  void apply_transition(HbmCommand command, std::uint32_t target_row,
                        std::size_t local_bank);
  void update_write_drain();
  void update_starvation(SimTime now);
  void prepare_refresh_for_arrival(SimTime now);
  void apply_idle_refresh(SimTime when);
  void issue_maintenance(MaintenanceCandidate candidate, SimTime now);
  [[nodiscard]] std::optional<HbmIssuedAccess> issue(Candidate candidate,
                                                     SimTime now);
  void record_command(HbmCommand command, const HbmAddress& address,
                      std::size_t local_bank, SimTime now);

  HbmControllerConfig config_;
  const IHbmAddressMapper& mapper_;
  HbmStats& stats_;
  HbmTimingEngine timing_engine_;
  HbmCommandPlanner command_planner_;
  const IScheduler& scheduler_;
  std::unique_ptr<IRowPolicy> row_policy_;
  std::unique_ptr<IRefreshManager> refresh_manager_;
  std::deque<HbmAccess> read_queue_;
  std::deque<HbmAccess> write_queue_;
  std::vector<HbmBankState> banks_;
  std::vector<SimTime> data_bus_ready_at_;
  SimTime unified_command_bus_ready_at_ = 0;
  SimTime row_command_bus_ready_at_ = 0;
  SimTime column_command_bus_ready_at_ = 0;
  SimTime refresh_busy_until_ = 0;
  SimTime refresh_stall_accounted_until_ = 0;
  std::optional<SimTime> wakeup_at_;
  std::optional<SimTime> refresh_due_at_;
  std::optional<SimTime> next_refresh_due_;
  bool draining_writes_ = false;
  bool refresh_pending_ = false;
  std::uint64_t outstanding_accesses_ = 0;
  std::uint32_t next_per_bank_refresh_ = 0;
  std::size_t reserved_reads_ = 0;
  std::size_t reserved_writes_ = 0;
};

}  // namespace hbmsim
