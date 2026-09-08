#include "hbmsim/hbm_system.h"
#include "hbmsim/kernel/event_queue.h"

#include <cassert>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

class TestSystem {
 public:
  explicit TestSystem(hbmsim::HbmConfig config)
      : core_(retain(std::move(config)), events_) {}

  hbmsim::SubmitResult submit(const hbmsim::HbmTransaction& transaction) {
    if (transaction.arrival_time <= events_.now()) {
      return core_.try_submit_now(transaction);
    }
    events_.schedule_at(transaction.arrival_time, [this, transaction] {
      const auto result = core_.try_submit_now(transaction);
      if (!result.accepted()) throw std::runtime_error(result.message);
    });
    return {hbmsim::SubmitStatus::Accepted,
            hbmsim::RequestToken{transaction.id}, {}};
  }

  hbmsim::SubmitResult try_submit_now(
      const hbmsim::HbmTransaction& transaction) {
    return core_.try_submit_now(transaction);
  }
  void run() { events_.run(); }
  void run_until(hbmsim::SimTime until) { events_.run_until(until); }
  hbmsim::SimTime now() const { return events_.now(); }
  void set_completion_callback(hbmsim::CompletionCallback callback) {
    core_.set_completion_callback(std::move(callback));
  }
  const auto& completions() const { return core_.completions(); }
  const auto& stats() const { return core_.stats(); }
  auto map_address(std::uint64_t address) const {
    return core_.map_address(address);
  }
  std::size_t active_request_count() const {
    return core_.active_request_count();
  }

 private:
  static hbmsim::HbmConfig retain(hbmsim::HbmConfig config) {
    config.simulation.retain_completions = true;
    return config;
  }

  hbmsim::EventQueue events_;
  hbmsim::HbmSystem core_;
};

hbmsim::HbmConfig base_config() {
  hbmsim::HbmConfig config;
  config.device.organization.topology.pseudo_channels_per_channel = 1;
  config.device.organization.topology.bank_groups_per_pseudo_channel = 1;
  config.device.organization.topology.banks_per_bank_group = 1;
  config.device.organization.address_interleave_bytes = 64;
  config.device.organization.columns_per_row = 2;
  config.device.organization.rows_per_bank = 8;
  config.device.organization.pseudo_channel_rate = {1'000, 1'000};
  config.device.timing.t_rcd = 10;
  config.device.timing.t_rp = 5;
  config.device.timing.t_cl = 20;
  config.device.timing.t_ras = 0;
  config.device.timing.t_rc = 0;
  config.device.timing.t_ccd = 0;
  config.device.timing.t_rrd = 0;
  config.device.timing.t_faw = 0;
  config.device.timing.t_wtr = 0;
  config.device.timing.t_rtw = 0;
  config.device.timing.t_rfc = 100;
  return config;
}

}  // namespace

int main() {
  // V0.2: the HBM2_2000 profile is the explicit validation baseline.
  {
    const auto config = hbmsim::HbmConfig::hbm2_2000();
    assert(config.device.standard->name() == "HBM2");
    assert(config.device.organization.topology.pseudo_channels_per_channel == 2);
    assert(config.device.organization.topology.bank_groups_per_pseudo_channel == 4);
    assert(config.device.organization.topology.banks_per_bank_group == 4);
    assert(config.device.organization.address_interleave_bytes == 32);
    assert(config.device.organization.physical_burst_bytes == 32);
    assert(config.device.timing.t_rcd == 14'000);
    assert(config.device.timing.t_rc == 48'000);
    assert(config.device.timing.t_rfc == 260'000);
    TestSystem system(config);
    const auto mapped = system.map_address((1ULL << 5) | (2ULL << 6) |
                                           (3ULL << 8) | (1ULL << 10) |
                                           (4ULL << 11) | (5ULL << 16));
    assert(mapped.pseudo_channel == 1);
    assert(mapped.bank_group == 2);
    assert(mapped.bank == 3);
    assert(mapped.column == 4);
    assert(mapped.row == 5);
  }

  // V0.3: HBM pseudo-channels have independent data buses.  Two 32 B reads
  // accepted at the same time complete together when they select PC 0/1.
  {
    auto config = hbmsim::HbmConfig::hbm2_2000();
    // This isolates pseudo-channel data resources from standard command-bus
    // occupancy; the standard profile itself is covered separately above.
    config.device.standard.reset();
    config.device.timing.t_rcd = 0;
    config.device.timing.t_command = 0;
    config.device.timing.t_rcd_rd = 0;
    config.device.timing.t_rcd_wr = 0;
    config.device.timing.t_cl = 0;
    config.device.timing.t_ccd = 0;
    config.device.timing.t_ccd_s = 0;
    config.device.timing.t_ccd_l = 0;
    config.device.timing.t_rrd = 0;
    config.device.timing.t_rrd_s = 0;
    config.device.timing.t_rrd_l = 0;
    config.device.timing.t_faw = 0;
    config.device.timing.t_wtr_s = 0;
    config.device.timing.t_wtr_l = 0;
    config.device.organization.pseudo_channel_rate = {16, 1'000};
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 32, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 32, 32, 0, 0}).accepted());
    system.run();
    assert(system.completions().size() == 2);
    assert(system.completions()[0].completion_time == 2'000);
    assert(system.completions()[1].completion_time == 2'000);
    assert(system.stats().channels[0].data_bus_busy_time == 4'000);
  }

  // V0.5: HBM3 is a resolved standard profile, not an enum-only label.
  {
    const auto config = hbmsim::HbmConfig::hbm3_6400();
    assert(config.device.standard->name() == "HBM3");
    assert(config.device.timing.use_extended_hbm_timing);
    assert(config.device.timing.t_rcd_rd == 19'375);
    assert(config.device.timing.t_rcd_wr == 9'375);
    assert(config.device.timing.t_ccd_l == 2'500);
    assert(config.device.timing.t_rfcpb == 200'000);
  }

  // H2: a closed-row request plans ACT then waits tRCD before RD.
  {
    TestSystem system(base_config());
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    system.run();
    assert(system.completions().size() == 1);
    assert(system.completions()[0].completion_time == 94);  // 10 + 20 + 64 ps
    assert(system.stats().issued_commands == 2);
    assert(system.stats().row_closed == 1);
  }

  // H2: hit, closed, and conflict are generated by actual command sequences.
  {
    TestSystem system(base_config());
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 64, 64, 100, 0}).accepted());
    assert(system.submit({3, hbmsim::HbmOp::Read, 128, 64, 200, 0}).accepted());
    system.run();
    const auto& completions = system.completions();
    assert(completions.size() == 3);
    assert(completions[0].access_class == hbmsim::HbmAccessClass::RowClosed);
    assert(completions[1].access_class == hbmsim::HbmAccessClass::RowHit);
    assert(completions[2].access_class == hbmsim::HbmAccessClass::RowConflict);
    assert(completions[0].completion_time == 94);
    assert(completions[1].completion_time == 184);
    assert(completions[2].completion_time == 299);
    assert(system.stats().row_closed == 1);
    assert(system.stats().row_hits == 1);
    assert(system.stats().row_conflicts == 1);
  }

  // H3: FR-FCFS keeps a row hit ahead of an older row-conflict request.
  {
    TestSystem system(base_config());
    std::vector<hbmsim::TransactionId> completion_order;
    system.set_completion_callback(
        [&](const hbmsim::HbmCompletion& completion) { completion_order.push_back(completion.id); });
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 128, 64, 100, 0}).accepted());
    assert(system.submit({3, hbmsim::HbmOp::Read, 64, 64, 100, 0}).accepted());
    system.run();
    assert((completion_order == std::vector<hbmsim::TransactionId>{1, 3, 2}));
    assert(system.completions()[1].access_class == hbmsim::HbmAccessClass::RowHit);
  }

  // H3: a write queue at its high watermark is drained before reads.
  {
    auto config = base_config();
    config.controller.write_drain_high_watermark = 1;
    config.controller.write_drain_low_watermark = 0;
    TestSystem system(config);
    std::vector<hbmsim::TransactionId> completion_order;
    system.set_completion_callback(
        [&](const hbmsim::HbmCompletion& completion) { completion_order.push_back(completion.id); });
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Write, 64, 64, 0, 0}).accepted());
    system.run();
    assert(completion_order.front() == 2);
  }

  // H4: all-bank refresh blocks the controller for tRFC before queued work.
  {
    auto config = base_config();
    config.controller.refresh_interval = 100;
    config.device.timing.t_rcd = 1000;
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 64, 64, 2, 0}).accepted());
    system.run_until(150);
    assert(system.stats().channels[0].refreshes == 1);
    assert(system.stats().completed_transactions == 0);
  }

  // H5: 16 KiB is represented by four 4 KiB accesses, never 256 64 B bursts.
  {
    auto config = base_config();
    config.device.organization.address_interleave_bytes = 4096;
    config.simulation.simulation_access_granularity_bytes = 4096;
    config.device.timing.t_rcd = 0;
    config.device.timing.t_cl = 0;
    config.device.organization.pseudo_channel_rate = {100'000, 1'000};
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 16 * 1024, 0, 0}).accepted());
    system.run();
    assert(system.stats().modeled_accesses == 4);
    assert(system.stats().completed_transactions == 1);
    assert(system.stats().read_bytes == 16 * 1024);
    assert(system.stats().channels[0].completed_bytes == 16 * 1024);
    assert(system.stats().channels[0].data_bus_busy_time == 164);
    assert(system.completions().front().latency_breakdown.total() ==
           system.completions().front().latency);
  }

  // V0.1: aggregation cannot hide channel interleaving. Four 64 B bursts
  // alternate between two channels and therefore remain four modelled accesses.
  {
    auto config = base_config();
    config.device.organization.topology.channels_per_stack = 2;
    config.device.organization.columns_per_row = 16;
    config.simulation.simulation_access_granularity_bytes = 4096;
    config.device.timing.t_rcd = 0;
    config.device.timing.t_cl = 0;
    config.device.organization.pseudo_channel_rate = {100'000, 1'000};
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 256, 0, 0}).accepted());
    system.run();
    assert(system.stats().modeled_accesses == 4);
    assert(system.stats().channels[0].completed_bytes == 128);
    assert(system.stats().channels[1].completed_bytes == 128);
  }

  // V0.1: adjacent bursts in one row may coalesce, but a row boundary ends
  // the group even when the configured aggregation limit is larger.
  {
    auto config = base_config();
    config.device.organization.columns_per_row = 2;
    config.simulation.simulation_access_granularity_bytes = 4096;
    config.device.timing.t_rcd = 0;
    config.device.timing.t_cl = 0;
    config.device.organization.pseudo_channel_rate = {100'000, 1'000};
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 256, 0, 0}).accepted());
    system.run();
    assert(system.stats().modeled_accesses == 2);
    assert(system.stats().completed_transactions == 1);
  }

  // H6: per-bank refresh rotates banks without using the all-bank blocker.
  {
    auto config = base_config();
    config.device.organization.topology.banks_per_bank_group = 2;
    config.controller.refresh_policy = hbmsim::RefreshPolicy::PerBank;
    config.controller.refresh_interval = 10;
    config.device.timing.t_rcd = 100;
    config.device.timing.t_rfcpb = 10;
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    system.run_until(25);
    assert(system.stats().channels[0].refreshes == 0);
    assert(system.stats().channels[0].per_bank_refreshes == 2);
  }

  // V0.1: an idle refresh closes an open row and is caught up lazily before
  // the next transaction, rather than creating a false row hit after idle.
  {
    auto config = base_config();
    config.controller.refresh_interval = 100;
    config.device.timing.t_rcd = 0;
    config.device.timing.t_cl = 0;
    config.device.timing.t_rfc = 10;
    config.device.organization.pseudo_channel_rate = {1'000, 1'000};
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 0, 64, 500, 0}).accepted());
    system.run_until(575);
    assert(system.completions().size() == 2);
    assert(system.completions()[1].access_class == hbmsim::HbmAccessClass::RowClosed);
    assert(system.stats().channels[0].refreshes == 5);
  }

  // H6: HBM4 RFM is threshold-triggered. Alternate two rows in one bank so
  // four real-profile requests trigger RFM; each maintenance close forces the
  // interrupted request to reactivate, so the deliberately tiny threshold
  // produces three events.
  {
    auto config = hbmsim::HbmConfig::hbm4_8000();
    config.controller.enable_rfm = true;
    config.controller.rfm_activation_threshold = 2;
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 32, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 8'192, 32, 1'000'000, 0}).accepted());
    assert(system.submit({3, hbmsim::HbmOp::Read, 0, 32, 2'000'000, 0}).accepted());
    assert(system.submit({4, hbmsim::HbmOp::Read, 8'192, 32, 3'000'000, 0}).accepted());
    system.run();
    assert(system.stats().rfm_events == 3);
    assert(system.stats().channels[0].rfm_events == 3);
    assert(system.completions().size() == 4);
  }

  // V0.6: rational transport rates represent HBM3's 25.6 B/ns exactly.
  {
    const hbmsim::BandwidthRate rate{32, 1'250};
    assert(rate.transfer_time(32) == 1'250);
    assert(rate.transfer_time(64) == 2'500);
  }

  // V0.6: future arrivals reserve finite controller queue capacity and
  // receive explicit backpressure instead of growing an unbounded queue.
  {
    auto config = base_config();
    config.controller.read_queue_capacity = 1;
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    const auto rejected = system.submit({2, hbmsim::HbmOp::Read, 64, 64, 0, 0});
    assert(rejected.status == hbmsim::SubmitStatus::Backpressure);
    system.run();
    assert(system.submit({2, hbmsim::HbmOp::Read, 64, 64,
                          system.now(), 0}).accepted());
    system.run();
    assert(system.stats().rejected_transactions == 1);
    assert(system.stats().channels[0].queue.max_read_depth == 1);
  }

  // V0.5: a closed-row standard policy emits auto-precharge data commands;
  // the next same-row request therefore starts closed without a separate PRE.
  {
    auto config = hbmsim::HbmConfig::hbm2_2000();
    config.controller.row_policy = hbmsim::RowPolicy::Closed;
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 32, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 0, 32, 100'000, 0}).accepted());
    system.run();
    assert(system.stats().row_closed == 2);
    assert(system.stats().row_hits == 0);
    assert(system.stats().act_commands == 2);
    assert(system.stats().pre_commands == 0);
    assert(system.stats().channels[0].command_bus.busy_time > 0);
    assert(system.stats().channels[0].pseudo_channels[0].busy_time == 4'000);
    assert(system.stats().channels[0].banks[0].issued_commands == 4);
  }

  // V0.5: HBM3's declared RFM command support is enforced consistently by
  // configuration validation and controller issue checks.
  {
    auto config = hbmsim::HbmConfig::hbm3_6400();
    config.controller.enable_rfm = true;
    config.controller.rfm_activation_threshold = 4;
    config.validate();
  }

  // V0.6.4: writes use tCWL while reads use tCL, and the exclusive stage
  // breakdown sums to the externally visible latency.
  {
    auto config = hbmsim::HbmConfig::hbm2_2000();
    config.controller.request_merge_policy = hbmsim::RequestMergePolicy::None;
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Write, 0, 32, 0, 0}).accepted());
    system.run();
    const auto& completion = system.completions().front();
    assert(completion.completion_time == 19'000);
    assert(completion.latency_breakdown.data_ready == 5'000);
    assert(completion.latency_breakdown.total() == completion.latency);
  }

  // V0.6.4: HBM row and column command buses can both issue at the same
  // timestamp. The second ACT overlaps the first RD at 14 ns.
  {
    auto config = hbmsim::HbmConfig::hbm2_2000();
    config.controller.request_merge_policy = hbmsim::RequestMergePolicy::None;
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 32, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 1ULL << 8, 32,
                          14'000, 0}).accepted());
    system.run();
    assert(system.completions().at(1).completion_time == 44'000);
    const auto& channel = system.stats().channels.front();
    assert(channel.row_command_bus.issued_commands == 2);
    assert(channel.column_command_bus.issued_commands == 2);
  }

  // V0.6.4: optional same-address merging retains two logical completions
  // while consuming one data command and one physical transfer.
  {
    auto config = base_config();
    config.controller.request_merge_policy = hbmsim::RequestMergePolicy::SameAddressRead;
    TestSystem system(config);
    assert(system.submit({1, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    assert(system.submit({2, hbmsim::HbmOp::Read, 0, 64, 0, 0}).accepted());
    system.run();
    assert(system.completions().size() == 2);
    assert(system.stats().read_commands == 1);
    assert(system.stats().channels[0].queue.merged_accesses == 1);
    assert(system.stats().channels[0].data_bus_busy_time == 64);
  }

  // V0.7.1: the reusable core only admits arrived work, streams completions by
  // default, and releases transaction IDs after completion.
  {
    auto config = base_config();
    hbmsim::EventQueue events;
    hbmsim::HbmSystem core(config, events);
    hbmsim::HbmTransaction transaction{
        9, hbmsim::HbmOp::Read, 0, 64, 10, 0,
        {hbmsim::TrafficClass::Fill, 7, 0xabc, 3}};
    assert(core.try_submit_now(transaction).status ==
           hbmsim::SubmitStatus::ArrivalInFuture);
    transaction.arrival_time = 0;
    std::vector<hbmsim::HbmCompletion> streamed;
    core.set_completion_callback(
        [&](const auto& completion) { streamed.push_back(completion); });
    assert(core.try_submit_now(transaction).accepted());
    events.run();
    assert(core.completions().empty());
    assert(core.active_request_count() == 0);
    assert(streamed.size() == 1);
    assert(streamed.front().metadata.traffic_class ==
           hbmsim::TrafficClass::Fill);
    assert(streamed.front().metadata.priority == 7);
    assert(streamed.front().metadata.opaque_tag == 0xabc);
    transaction.arrival_time = events.now();
    assert(core.try_submit_now(transaction).accepted());
    events.run();
    assert(streamed.size() == 2);
  }

  // V0.7.1: finite queues advertise a capacity transition instead of forcing
  // an integration driver to poll blindly.
  {
    auto config = base_config();
    config.controller.read_queue_capacity = 1;
    hbmsim::EventQueue events;
    hbmsim::HbmSystem core(config, events);
    std::size_t notifications = 0;
    std::vector<hbmsim::HbmCompletion> completed;
    core.set_capacity_callback([&](auto) { ++notifications; });
    core.set_completion_callback(
        [&](const auto& completion) { completed.push_back(completion); });
    assert(core.try_submit_now(
                    {1, hbmsim::HbmOp::Read, 0, 64, 0, 0})
               .accepted());
    assert(core.try_submit_now(
                    {2, hbmsim::HbmOp::Read, 64, 64, 0, 0})
               .status == hbmsim::SubmitStatus::Backpressure);
    while (notifications == 0) assert(events.run_next());
    assert(core.try_submit_now(
                    {2, hbmsim::HbmOp::Read, 64, 64, 0, 0})
               .accepted());
    events.run();
    assert(completed.size() == 2);
    assert(completed.back().latency_breakdown.total() ==
           completed.back().latency);
  }
}
