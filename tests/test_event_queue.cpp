#include "hbmsim/kernel/event_queue.h"

#include <cassert>
#include <stdexcept>
#include <vector>

int main() {
  hbmsim::EventQueue events;
  std::vector<int> order;
  events.schedule_at(10, [&] { order.push_back(1); });
  events.schedule_at(10, [&] { order.push_back(2); });
  events.schedule_at(5, [&] { order.push_back(0); });

  events.run_until(5);
  assert((order == std::vector<int>{0}));
  assert(events.now() == 5);
  events.run();
  assert((order == std::vector<int>{0, 1, 2}));
  assert(events.now() == 10);

  bool rejected_past_event = false;
  try {
    events.schedule_at(9, [] {});
  } catch (const std::invalid_argument&) {
    rejected_past_event = true;
  }
  assert(rejected_past_event);

  hbmsim::EventQueue cancellable;
  bool called = false;
  const auto token = cancellable.schedule_at(7, [&] { called = true; });
  assert(cancellable.cancel(token));
  assert(cancellable.empty());
  cancellable.run();
  assert(!called);
}
