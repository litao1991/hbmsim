#include "hbmsim/kernel/event_queue.h"

#include <stdexcept>
#include <utility>

namespace hbmsim {

EventToken EventQueue::schedule_at(SimTime when, EventCallback callback) {
  if (when < now_) {
    throw std::invalid_argument("cannot schedule an event in the past");
  }
  const auto token = next_token_++;
  events_.push(Event{when, next_sequence_++, token, std::move(callback)});
  return token;
}

bool EventQueue::cancel(EventToken token) {
  return token != 0 && cancelled_.insert(token).second;
}

bool EventQueue::empty() {
  discard_cancelled();
  return events_.empty();
}

SimTime EventQueue::next_time() {
  discard_cancelled();
  if (events_.empty()) throw std::logic_error("event queue is empty");
  return events_.top().time;
}

void EventQueue::discard_cancelled() {
  while (!events_.empty() && cancelled_.erase(events_.top().token) != 0) {
    events_.pop();
  }
}

void EventQueue::dispatch_one() {
  discard_cancelled();
  if (events_.empty()) return;
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
  discard_cancelled();
  while (!events_.empty() && events_.top().time <= until) {
    dispatch_one();
    discard_cancelled();
  }
  now_ = until;
}

bool EventQueue::run_next() {
  discard_cancelled();
  if (events_.empty()) return false;
  dispatch_one();
  return true;
}

}  // namespace hbmsim
