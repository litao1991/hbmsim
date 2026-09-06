#include "hbmsim/kernel/event_queue.h"

#include <cassert>
#include <stdexcept>
#include <vector>

int main() {
  hbmsim::EventQueue events;
  std::vector<int> order;
  events.schedule(10, hbmsim::EventType::TransactionArrival,
                  [&] { order.push_back(1); });
  events.schedule(10, hbmsim::EventType::TransactionCompletion,
                  [&] { order.push_back(2); });
  events.schedule(5, hbmsim::EventType::ControllerWakeup,
                  [&] { order.push_back(0); });

  events.run_until(5);
  assert((order == std::vector<int>{0}));
  assert(events.now() == 5);
  events.run();
  assert((order == std::vector<int>{0, 1, 2}));
  assert(events.now() == 10);

  bool rejected_past_event = false;
  try {
    events.schedule(9, hbmsim::EventType::TransactionArrival, [] {});
  } catch (const std::invalid_argument&) {
    rejected_past_event = true;
  }
  assert(rejected_past_event);
}
