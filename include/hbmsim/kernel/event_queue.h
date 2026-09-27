#pragma once

#include "hbmsim/common/types.h"
#include "hbmsim/kernel/sim_scheduler.h"

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
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
  void dispatch_one();

  SimTime now_ = 0;
  std::uint64_t next_sequence_ = 0;
  EventToken next_token_ = 1;
  using Key = std::pair<SimTime, std::uint64_t>;
  std::map<Key, Event> events_;
  std::unordered_map<EventToken, Key> tokens_;
};

}  // namespace hbmsim
