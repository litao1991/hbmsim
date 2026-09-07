#pragma once

#include "hbmsim/controller/command_planner.h"
#include "hbmsim/controller/scheduler.h"
#include "hbmsim/kernel/event_queue.h"
#include "hbmsim/mapping/address_mapper.h"
#include "hbmsim/media/bank_state.h"
#include "hbmsim/timing/timing_engine.h"
#include "hbmsim/topology.h"
#include "hbmsim/transaction.h"

#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hbmsim {

enum class HbmStandard { Hbm2, Hbm3, Hbm4 };
enum class RefreshPolicy { AllBank, PerBank };

struct HbmConfig {
  HbmTopology topology{};
  HbmTimingSpec timing{};
  HbmStandard standard = HbmStandard::Hbm3;
  RowPolicy row_policy = RowPolicy::Open;
  SchedulerKind scheduler = SchedulerKind::FrFcfs;
  RefreshPolicy refresh_policy = RefreshPolicy::AllBank;
  std::uint64_t channel_bandwidth_bytes_per_ns = 32;
  std::uint64_t address_interleave_bytes = 64;
  AddressMapping address_mapping = AddressMapping::Linear;
  std::uint32_t columns_per_row = 128;
  std::uint32_t rows_per_bank = 16'384;
  std::size_t write_drain_high_watermark = 16;
  std::size_t write_drain_low_watermark = 4;
  SimTime refresh_interval = 0;
  bool enable_rfm = false;
  std::uint32_t rfm_activation_threshold = 0;
  std::uint64_t physical_burst_bytes = 64;
  // Zero models every physical burst. A positive value coalesces only
  // contiguous bursts that remain in the same channel/PC/BG/bank/row.
  std::uint64_t simulation_access_granularity_bytes = 0;

  void validate() const;
  [[nodiscard]] static HbmConfig hbm2_2000();
  [[nodiscard]] static HbmConfig hbm4_8000();
};

struct ChannelStats {
  std::uint64_t completed_bytes = 0;
  SimTime data_bus_busy_time = 0;
  std::uint64_t refreshes = 0;
  std::uint64_t per_bank_refreshes = 0;
  std::uint64_t rfm_events = 0;
};

struct HbmStats {
  std::uint64_t submitted_transactions = 0;
  std::uint64_t completed_transactions = 0;
  std::uint64_t modeled_accesses = 0;
  std::uint64_t issued_commands = 0;
  std::uint64_t read_bytes = 0;
  std::uint64_t write_bytes = 0;
  std::uint64_t row_hits = 0;
  std::uint64_t row_closed = 0;
  std::uint64_t row_conflicts = 0;
  std::uint64_t act_commands = 0;
  std::uint64_t pre_commands = 0;
  std::uint64_t read_commands = 0;
  std::uint64_t write_commands = 0;
  std::uint64_t rfm_events = 0;
  std::vector<ChannelStats> channels;
};

using CompletionCallback = std::function<void(const HbmCompletion&)>;

class HbmSystem {
 public:
  explicit HbmSystem(HbmConfig config = {});

  SubmitResult submit(const HbmTransaction& transaction);
  void set_completion_callback(CompletionCallback callback);
  void run();
  void run_until(SimTime until);

  [[nodiscard]] SimTime now() const { return event_queue_.now(); }
  [[nodiscard]] const HbmStats& stats() const { return stats_; }
  [[nodiscard]] const std::vector<HbmCompletion>& completions() const {
    return completions_;
  }
  [[nodiscard]] HbmAddress map_address(std::uint64_t address) const {
    return address_mapper_.map(address);
  }

 private:
  struct Access {
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
  struct ParentRequest {
    HbmTransaction transaction{};
    std::uint64_t remaining_accesses = 0;
    SimTime completion_time = 0;
    std::uint32_t completion_channel = 0;
    HbmAccessClass last_access_class = HbmAccessClass::RowClosed;
  };
  struct ChannelState {
    // HBM pseudo-channels have independent data paths.  Channel-wide timing
    // constraints remain in the timing engine; only data transfer ownership
    // is tracked per pseudo-channel here.
    std::vector<SimTime> data_bus_ready_at;
    SimTime refresh_busy_until = 0;
    std::deque<Access> read_queue;
    std::deque<Access> write_queue;
    std::optional<SimTime> wakeup_at;
    std::optional<SimTime> refresh_due_at;
    bool draining_writes = false;
    bool refresh_pending = false;
    std::uint64_t outstanding_accesses = 0;
    std::uint32_t next_per_bank_refresh = 0;
    // `next_refresh_due` remains meaningful while idle.  `refresh_due_at`
    // exists only when a corresponding event is actually queued.
    std::optional<SimTime> next_refresh_due;
  };
  struct Candidate {
    bool is_write = false;
    std::size_t index = 0;
    HbmCommand command = HbmCommand::Act;
    SimTime ready_at = 0;
    int priority = 0;
  };
  struct MaintenanceCandidate {
    HbmCommand command = HbmCommand::RefreshPerBank;
    std::uint32_t bank = 0;
    SimTime ready_at = 0;
  };

  [[nodiscard]] SimTime transfer_time(std::uint64_t bytes) const;
  [[nodiscard]] std::vector<Access> split_transaction(
      const HbmTransaction& transaction) const;
  [[nodiscard]] std::optional<Candidate> choose_next(std::uint32_t channel,
                                                       SimTime now) const;
  [[nodiscard]] std::optional<Candidate> choose_with_policy(
      std::uint32_t channel, SimTime now) const;
  [[nodiscard]] std::optional<MaintenanceCandidate> choose_maintenance(
      std::uint32_t channel, SimTime now) const;
  void admit(const HbmTransaction& transaction);
  void schedule_controller_wake(std::uint32_t channel, SimTime when);
  void drive_controller(std::uint32_t channel);
  void issue(std::uint32_t channel, Candidate candidate, SimTime now);
  void issue_maintenance(std::uint32_t channel,
                         MaintenanceCandidate candidate, SimTime now);
  void finish_access(const Access& access, std::uint32_t channel,
                     SimTime completion_time);
  void complete_parent(TransactionId id);
  void prepare_refresh_for_arrival(std::uint32_t channel, SimTime now);
  void schedule_refresh_due(std::uint32_t channel);
  void apply_idle_refresh(std::uint32_t channel, SimTime when);
  void issue_all_bank_refresh(std::uint32_t channel, SimTime now);
  void refresh_due(std::uint32_t channel);

  HbmConfig config_;
  EventQueue event_queue_;
  HbmAddressMapper address_mapper_;
  HbmTimingEngine timing_engine_;
  HbmCommandPlanner command_planner_;
  std::vector<ChannelState> channels_;
  std::vector<HbmBankState> banks_;
  HbmStats stats_;
  CompletionCallback completion_callback_;
  std::unordered_set<TransactionId> known_transaction_ids_;
  std::unordered_map<TransactionId, ParentRequest> parents_;
  std::vector<HbmCompletion> completions_;
  std::uint64_t next_access_sequence_ = 0;
};

}  // namespace hbmsim
