#pragma once

#include "hbmsim/controller/hbm_controller.h"
#include "hbmsim/dram/hbm_standard.h"
#include "hbmsim/kernel/sim_scheduler.h"
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

struct DeviceSpec {
  HbmOrganization organization{};
  HbmTimingSpec timing{};
  std::shared_ptr<const HbmStandard> standard;

  [[nodiscard]] static DeviceSpec custom();
  [[nodiscard]] static DeviceSpec from_standard(
      std::shared_ptr<const HbmStandard> standard);
};

enum class RequestMergePolicy { None, SameAddressRead };

struct ControllerConfig {
  RowPolicy row_policy = RowPolicy::Open;
  SchedulerKind scheduler = SchedulerKind::FrFcfs;
  RefreshPolicy refresh_policy = RefreshPolicy::AllBank;
  std::size_t write_drain_high_watermark = 16;
  std::size_t write_drain_low_watermark = 4;
  std::size_t read_queue_capacity = 0;
  std::size_t write_queue_capacity = 0;
  RequestMergePolicy request_merge_policy = RequestMergePolicy::None;
  SimTime starvation_threshold = 0;
  SimTime refresh_interval = 0;
  bool enable_rfm = false;
  std::uint32_t rfm_activation_threshold = 0;
};

struct SimulationConfig {
  std::uint64_t simulation_access_granularity_bytes = 0;
  bool retain_completions = false;
  bool detailed_stats = true;
};

// Device-standard truth, runtime controller policies and simulation-only
// concerns deliberately live in separate configuration domains.
struct HbmConfig {
  DeviceSpec device = DeviceSpec::custom();
  ControllerConfig controller{};
  SimulationConfig simulation{};

  void validate() const;
  [[nodiscard]] static HbmConfig hbm2_2000();
  [[nodiscard]] static HbmConfig hbm3_6400();
  [[nodiscard]] static HbmConfig hbm4_8000();
};

using CompletionCallback = std::function<void(const HbmCompletion&)>;
struct CapacityNotification {
  SimTime time = 0;
  std::uint32_t channel = 0;
  std::size_t read_slots = 0;
  std::size_t write_slots = 0;
};
using CapacityCallback = std::function<void(const CapacityNotification&)>;

class HbmSystem {
 public:
  HbmSystem(HbmConfig config, ISimScheduler& scheduler);
  ~HbmSystem();
  HbmSystem(const HbmSystem&) = delete;
  HbmSystem& operator=(const HbmSystem&) = delete;

  SubmitResult try_submit_now(const HbmTransaction& transaction);
  void set_completion_callback(CompletionCallback callback);
  void set_capacity_callback(CapacityCallback callback);

  [[nodiscard]] SimTime now() const { return scheduler_.now(); }
  [[nodiscard]] const HbmStats& stats() const { return stats_; }
  [[nodiscard]] const std::vector<HbmCompletion>& completions() const {
    return completions_;
  }
  [[nodiscard]] HbmAddress map_address(std::uint64_t address) const {
    return address_mapper_->map(address);
  }
  [[nodiscard]] std::size_t active_request_count() const {
    return parents_.size();
  }

 private:
  using Access = HbmAccess;
  struct ParentRequest {
    HbmTransaction transaction{};
    std::uint64_t remaining_accesses = 0;
    SimTime completion_time = 0;
    std::uint32_t completion_channel = 0;
    HbmAccessClass last_access_class = HbmAccessClass::RowClosed;
    HbmLatencyBreakdown last_latency_breakdown{};
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
  EventToken schedule_event(SimTime when, EventCallback callback);
  HbmConfig config_;
  ISimScheduler& scheduler_;
  std::unique_ptr<IHbmAddressMapper> address_mapper_;
  HbmStats stats_;
  std::vector<std::unique_ptr<HbmController>> controllers_;
  CompletionCallback completion_callback_;
  CapacityCallback capacity_callback_;
  std::unordered_map<TransactionId, ParentRequest> parents_;
  std::vector<HbmCompletion> completions_;
  std::unordered_set<EventToken> pending_events_;
  std::uint64_t next_access_sequence_ = 0;
};

}  // namespace hbmsim
