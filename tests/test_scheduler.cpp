#include "hbmsim/controller/scheduler.h"
#include "hbmsim/dram/address.h"
#include "hbmsim/dram/hbm_standard.h"

#include <cassert>
#include <vector>

int main() {
  const hbmsim::Address address{{hbmsim::AddressLevel::Channel, 0},
                                {hbmsim::AddressLevel::Bank, 2},
                                {hbmsim::AddressLevel::Row, 17}};
  assert(address.at(hbmsim::AddressLevel::Bank) == 2);
  assert(hbmsim::Hbm3Standard{}.supports(hbmsim::DramCommand::RfmPerBank));

  const std::vector<hbmsim::SchedulerCandidate> candidates{
      {0, false, hbmsim::DramCommand::Activate, 20, 1, false, false},
      {1, false, hbmsim::DramCommand::Read, 10, 2, true, true},
      {2, false, hbmsim::DramCommand::Read, 10, 3, false, true},
  };
  assert(hbmsim::scheduler_for(hbmsim::SchedulerKind::FrFcfsRowHit)
             .choose(candidates, 10)->queue_index == 1);
  // FIFO waits for the oldest command rather than allowing a newer row hit to
  // bypass it, which makes its policy difference explicit and deterministic.
  assert(hbmsim::scheduler_for(hbmsim::SchedulerKind::Fifo)
             .choose(candidates, 10)->queue_index == 0);

  const std::vector<hbmsim::SchedulerCandidate> priorities{
      {0, false, hbmsim::DramCommand::Read, 0, 1, true, true, 1},
      {1, false, hbmsim::DramCommand::Read, 0, 2, true, true, 9},
  };
  assert(hbmsim::scheduler_for(hbmsim::SchedulerKind::FrFcfs)
             .choose(priorities, 0)->queue_index == 1);
  assert(hbmsim::scheduler_for(hbmsim::SchedulerKind::FrFcfsRowHit)
             .choose(priorities, 0)->queue_index == 1);
}
