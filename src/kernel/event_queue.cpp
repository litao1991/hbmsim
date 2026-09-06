#include "hbmsim/kernel/event_queue.h"

#include <stdexcept>
#include <utility>

namespace hbmsim {

void EventQueue::schedule(SimTime when, EventType type,
                          std::function<void()> callback) {
  if (when < now_) {
    throw std::invalid_argument("cannot schedule an event in the past");
  }
  events_.push(Event{when, next_sequence_++, type, std::move(callback)});
}

void EventQueue::dispatch_one() {
  Event event = events_.top();
  events_.pop();
  now_ = event.time;
  event.callback();
}

void EventQueue::run() {
  while (!events_.empty()) {
    dispatch_one();
  }
}

void EventQueue::run_until(SimTime until) {
  if (until < now_) {
    throw std::invalid_argument("cannot run an event queue backwards in time");
  }
  while (!events_.empty() && events_.top().time <= until) {
    dispatch_one();
  }
  now_ = until;
}

}  // namespace hbmsim
