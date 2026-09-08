#pragma once

#include "hbmsim/controller/hbm_controller.h"
#include "hbmsim/dram/hbm_standard.h"
#include "hbmsim/kernel/event_queue.h"
#include "hbmsim/mapping/address_mapper.h"
#include "hbmsim/resource/bandwidth_rate.h"
#include "hbmsim/stats.h"
#include "hbmsim/topology.h"
#include "hbmsim/transaction.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hbmsim {

// Runtime controller policies and an already resolved standard profile.  The
// named constructors populate the complete organization/timing/mapping/rate
// bundle; direct field editing remains available for directed tests.
struct HbmConfig {
  HbmTopology topology{};
  HbmTimingSpec timing{};
  std::shared_ptr<const HbmStandard> standard;
  RowPolicy row_policy = RowPolicy::Open;
  SchedulerKind scheduler = SchedulerKind::FrFcfs;
  RefreshPolicy refresh_policy = RefreshPolicy::AllBank;
  BandwidthRate pseudo_channel_rate{32, 1'000};
  std::uint64_t address_interleave_bytes = 64;
  AddressMapping address_mapping = AddressMapping::Linear;
  std::uint32_t columns_per_row = 128;
  std::uint32_t rows_per_bank = 16'384;
  std::size_t write_drain_high_watermark = 16;
  std::size_t write_drain_low_watermark = 4;
  std::size_t read_queue_capacity = 0;
  std::size_t write_queue_capacity = 0;
  SimTime starvation_threshold = 0;
  SimTime refresh_interval = 0;
  bool enable_rfm = false;
  std::uint32_t rfm_activation_threshold = 0;
  std::uint64_t physical_burst_bytes = 64;
  std::uint64_t simulation_access_granularity_bytes = 0;

  void validate() const;
  [[nodiscard]] static HbmConfig hbm2_2000();
  [[nodiscard]] static HbmConfig hbm3_6400();
  [[nodiscard]] static HbmConfig hbm4_8000();
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
    return address_mapper_->map(address);
  }

 private:
  using Access = HbmAccess;
  struct ParentRequest {
    HbmTransaction transaction{};
    std::uint64_t remaining_accesses = 0;
    SimTime completion_time = 0;
    std::uint32_t completion_channel = 0;
    HbmAccessClass last_access_class = HbmAccessClass::RowClosed;
  };

  [[nodiscard]] std::vector<Access> split_transaction(
      const HbmTransaction& transaction) const;
  void admit(const HbmTransaction& transaction, std::vector<Access> accesses);
  void schedule_controller_wake(std::uint32_t channel, SimTime when);
  void schedule_refresh_due(std::uint32_t channel);
  void drive_controller(std::uint32_t channel);
  void finish_access(const Access& access, std::uint32_t channel,
                     SimTime completion_time);
  void complete_parent(TransactionId id);
  HbmConfig config_;
  EventQueue event_queue_;
  std::unique_ptr<IHbmAddressMapper> address_mapper_;
  HbmStats stats_;
  std::vector<std::unique_ptr<HbmController>> controllers_;
  CompletionCallback completion_callback_;
  std::unordered_set<TransactionId> known_transaction_ids_;
  std::unordered_map<TransactionId, ParentRequest> parents_;
  std::vector<HbmCompletion> completions_;
  std::uint64_t next_access_sequence_ = 0;
};

}  // namespace hbmsim
