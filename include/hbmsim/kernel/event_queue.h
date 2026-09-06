#pragma once

#include "hbmsim/common/types.h"

#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace hbmsim {

enum class EventType { TransactionArrival, TransactionCompletion, ControllerWakeup, RefreshDue };

struct Event {
  SimTime time = 0;
  std::uint64_t sequence = 0;
  EventType type = EventType::TransactionArrival;
  std::function<void()> callback;
};

struct EventCompare {
  bool operator()(const Event& left, const Event& right) const {
    return left.time != right.time ? left.time > right.time
                                   : left.sequence > right.sequence;
  }
};

class EventQueue {
 public:
  void schedule(SimTime when, EventType type, std::function<void()> callback);
  void run();
  void run_until(SimTime until);

  [[nodiscard]] bool empty() const { return events_.empty(); }
  [[nodiscard]] SimTime now() const { return now_; }
  [[nodiscard]] SimTime next_time() const { return events_.top().time; }

 private:
  void dispatch_one();

  SimTime now_ = 0;
  std::uint64_t next_sequence_ = 0;
  std::priority_queue<Event, std::vector<Event>, EventCompare> events_;
};

}  // namespace hbmsim
