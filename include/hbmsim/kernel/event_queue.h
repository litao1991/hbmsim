#pragma once

#include "hbmsim/common/types.h"
#include "hbmsim/kernel/sim_scheduler.h"

#include <cstdint>
#include <functional>
#include <queue>
#include <unordered_set>
#include <vector>

namespace hbmsim {

struct Event {
  SimTime time = 0;
  std::uint64_t sequence = 0;
  EventToken token = 0;
  EventCallback callback;
};

struct EventCompare {
  bool operator()(const Event& left, const Event& right) const {
    return left.time != right.time ? left.time > right.time
                                   : left.sequence > right.sequence;
  }
};

class EventQueue final : public ISimScheduler {
 public:
  EventToken schedule_at(SimTime when, EventCallback callback) override;
  bool cancel(EventToken token) override;
  void run();
  void run_until(SimTime until);
  bool run_next();

  [[nodiscard]] bool empty();
  [[nodiscard]] SimTime now() const noexcept override { return now_; }
  [[nodiscard]] SimTime next_time();

 private:
  void discard_cancelled();
  void dispatch_one();

  SimTime now_ = 0;
  std::uint64_t next_sequence_ = 0;
  EventToken next_token_ = 1;
  std::priority_queue<Event, std::vector<Event>, EventCompare> events_;
  std::unordered_set<EventToken> cancelled_;
};

}  // namespace hbmsim
