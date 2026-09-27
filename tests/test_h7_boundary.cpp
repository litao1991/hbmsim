#include "hbmsim/hbm_system.h"
#include "hbmsim/kernel/event_queue.h"

#include <cassert>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

int main() {
  using namespace hbmsim;
  // Lazy expansion: a single slot must serve a whole page, not reject it forever.
  for (auto size : {64u, 4096u, 16384u}) {
    auto config = HbmConfig::hbm2_2000();
    config.controller.write_queue_capacity = 1;
    config.controller.max_active_transactions = 1;
    EventQueue events;
    HbmSystem core(config, events);
    std::size_t completions = 0;
    core.set_completion_callback([&](const auto& c) {
      ++completions;
      assert(c.size_bytes == size);
      assert(c.latency == c.latency_breakdown.total());
      assert(core.active_request_count() == 0);
      // The just-completed ID is reusable from inside the callback.
      if (completions < 3)
        assert(core.try_submit_now({1, HbmOp::Write, 0, size, events.now()}).accepted());
    });
    assert(core.try_submit_now({1, HbmOp::Write, 0, size, 0}).accepted());
    assert(core.try_submit_now({2, HbmOp::Write, 0, 32, 0}).status == SubmitStatus::Backpressure);
    events.run();
    assert(completions == 3);
    assert(core.stats().write_bytes == size * 3);
    assert(core.stats().physical_write_bytes == size * 3);
    assert(core.stats().channels[0].queue.max_write_depth == 1);
    assert(core.completions().empty());
  }
  // Standard data cannot silently diverge from command behavior.
  auto config = HbmConfig::hbm2_2000();
  bool rejected = false;
  try { config.device.edit_timing().t_cl = 0; }
  catch (const std::logic_error&) { rejected = true; }
  assert(rejected);
  auto custom = config.device.as_custom();
  custom.edit_timing().t_cl = 0;
  assert(!custom.standard());
  assert(config.device.timing().t_cl != 0);

  // Merge fanout has two logical completions, but only one physical transfer.
  for (bool detailed : {true, false}) {
    config.simulation.detailed_stats = detailed;
    EventQueue events;
    HbmSystem core(config, events);
    std::size_t count = 0;
    core.set_completion_callback([&](const auto& c) {
      ++count;
      if (!detailed) assert(c.latency_breakdown.total() == 0);
    });
    assert(core.try_submit_now({1, HbmOp::Read, 0, 32, 0}).accepted());
    assert(core.try_submit_now({2, HbmOp::Read, 0, 32, 0}).accepted());
    events.run();
    assert(count == 2);
    assert(core.stats().modeled_accesses == 2);
    assert(core.stats().physical_accesses == 1);
    assert(core.stats().read_bytes == 64);
    assert(core.stats().physical_read_bytes == 32);
    if (!detailed) {
      assert(core.stats().channels[0].banks.empty());
      assert(core.stats().channels[0].queue.command_wait_time == 0);
    }
  }
  // Different offsets in the same burst are not identical reads.
  {
    EventQueue events;
    HbmSystem core(config, events);
    assert(core.try_submit_now({1, HbmOp::Read, 0, 8, 0}).accepted());
    assert(core.try_submit_now({2, HbmOp::Read, 8, 8, 0}).accepted());
    events.run();
    assert(core.stats().physical_accesses == 2);
    assert(core.try_submit_now({3, HbmOp::Read,
        std::numeric_limits<std::uint64_t>::max(), 2, events.now()}).status == SubmitStatus::InvalidArgument);
  }
  // Ordering-domain boundaries and queued writes forbid merging.
  for (bool with_write : {false, true}) {
    auto policy = HbmConfig::hbm2_2000();
    EventQueue clock;
    HbmSystem core(policy, clock);
    HbmTransaction first{1, HbmOp::Read, 0, 32, 0};
    HbmTransaction second{2, HbmOp::Read, 0, 32, 0};
    if (!with_write) second.metadata.ordering_domain = 1;
    assert(core.try_submit_now(first).accepted());
    if (with_write)
      assert(core.try_submit_now({3, HbmOp::Write, 0, 32, 0}).accepted());
    assert(core.try_submit_now(second).accepted());
    clock.run();
    assert(core.stats().physical_read_bytes == 64);
    assert(core.stats().channels[0].queue.merged_accesses == 0);
  }
  // Long streaming run retains no retired IDs or completion history.
  {
    auto bounded = HbmConfig::hbm2_2000();
    bounded.controller.max_active_transactions = 1;
    bounded.controller.read_queue_capacity = 1;
    EventQueue clock;
    HbmSystem core(bounded, clock);
    unsigned completed = 0;
    core.set_completion_callback([&](const auto&) {
      ++completed;
      assert(core.active_request_count() == 0);
      if (completed < 2000)
        assert(core.try_submit_now({1, HbmOp::Read, 0, 32, clock.now()}).accepted());
    });
    assert(core.try_submit_now({1, HbmOp::Read, 0, 32, 0}).accepted());
    clock.run();
    assert(completed == 2000);
    assert(core.completions().empty());
    assert(core.active_request_count() == 0);
  }
  // Cancellation releases captures now; completed and unknown tokens are inert.
  EventQueue events;
  auto payload = std::make_shared<int>(1);
  std::weak_ptr<int> weak = payload;
  const auto token = events.schedule_at(100, [payload] {});
  payload.reset();
  assert(events.cancel(token));
  assert(weak.expired());
  assert(!events.cancel(token));
  assert(!events.cancel(999999));
  const auto done = events.schedule_at(1, [] {});
  events.run();
  assert(!events.cancel(done));
  {
    HbmSystem core(config, events);
    assert(core.try_submit_now({1, HbmOp::Read, 0, 32, events.now()}).accepted());
  }
  assert(events.empty());
}
