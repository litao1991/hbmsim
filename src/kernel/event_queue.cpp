#include "hbmsim/kernel/event_queue.h"

#include <stdexcept>
#include <utility>

namespace hbmsim {

EventToken EventQueue::schedule_at(SimTime when, EventCallback callback) {
  if (when < now_) {
    throw std::invalid_argument("cannot schedule an event in the past");
  }
  const auto token = next_token_++;
  const Key key{when, next_sequence_++};
  events_.emplace(key, Event{when, key.second, token, std::move(callback)});
  tokens_.emplace(token, key);
  return token;
}

bool EventQueue::cancel(EventToken token) {
  const auto found = tokens_.find(token);
  if (found == tokens_.end()) return false;
  events_.erase(found->second);
  tokens_.erase(found);
  return true;
}

bool EventQueue::empty() {
  return events_.empty();
}

SimTime EventQueue::next_time() {
  if (events_.empty()) throw std::logic_error("event queue is empty");
  return events_.begin()->second.time;
}

void EventQueue::dispatch_one() {
  if (events_.empty()) return;
  Event event = std::move(events_.begin()->second);
  events_.erase(events_.begin());
  tokens_.erase(event.token);
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
  while (!events_.empty() && events_.begin()->second.time <= until) {
    dispatch_one();
  }
  now_ = until;
}

bool EventQueue::run_next() {
  if (events_.empty()) return false;
  dispatch_one();
  return true;
}

}  // namespace hbmsim
