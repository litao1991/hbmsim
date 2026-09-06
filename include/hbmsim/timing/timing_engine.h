#pragma once

#include "hbmsim/dram/command.h"
#include "hbmsim/mapping/address_mapper.h"
#include "hbmsim/timing/timing_spec.h"

#include <array>
#include <cstdint>
#include <deque>
#include <unordered_map>

namespace hbmsim {

class HbmTimingEngine {
 public:
  explicit HbmTimingEngine(HbmTimingSpec spec = {});

  [[nodiscard]] SimTime earliest_issue(HbmCommand command,
                                       const HbmAddress& address,
                                       SimTime now) const;
  void record(HbmCommand command, const HbmAddress& address, SimTime when);

 private:
  static constexpr std::size_t kCommandCount = 8;
  using CommandHistory = std::array<std::deque<SimTime>, kCommandCount>;

  [[nodiscard]] std::uint64_t resource_key(TimingScope scope,
                                           const HbmAddress& address) const;
  [[nodiscard]] static std::size_t command_index(HbmCommand command);

  std::vector<TimingConstraint> constraints_;
  std::unordered_map<std::uint64_t, CommandHistory> history_;
};

}  // namespace hbmsim
