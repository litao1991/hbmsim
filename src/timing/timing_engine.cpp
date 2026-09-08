#include "hbmsim/timing/timing_engine.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace hbmsim {

namespace {

HbmCommand timing_command(HbmCommand command) {
  switch (command) {
    case HbmCommand::PreAll: return HbmCommand::PreBank;
    case HbmCommand::ReadAuto: return HbmCommand::Read;
    case HbmCommand::WriteAuto: return HbmCommand::Write;
    default: return command;
  }
}

}  // namespace

HbmTimingEngine::HbmTimingEngine(HbmTimingSpec spec)
    : constraints_(spec.constraints()) {}

std::size_t HbmTimingEngine::command_index(HbmCommand command) {
  return static_cast<std::size_t>(command);
}

std::uint64_t HbmTimingEngine::resource_key(TimingScope scope,
                                            const HbmAddress& address) const {
  std::uint64_t index = 0;
  switch (scope) {
    case TimingScope::Bank: index = address.flat_bank; break;
    case TimingScope::BankGroup: index = address.flat_bank_group; break;
    case TimingScope::Sid: index = address.flat_sid; break;
    case TimingScope::PseudoChannel: index = address.flat_pseudo_channel; break;
    case TimingScope::Channel: index = address.flat_channel; break;
  }
  return (static_cast<std::uint64_t>(scope) << 56U) | index;
}

SimTime HbmTimingEngine::earliest_issue(HbmCommand command,
                                        const HbmAddress& address,
                                        SimTime now) const {
  command = timing_command(command);
  SimTime earliest = now;
  for (const auto& constraint : constraints_) {
    if (constraint.following != command) continue;
    const auto iterator = history_.find(resource_key(constraint.scope, address));
    if (iterator == history_.end()) continue;
    const auto& history = iterator->second[command_index(constraint.preceding)];
    if (history.size() < constraint.distance) continue;
    const auto preceding_time = history[history.size() - constraint.distance];
    if (preceding_time > std::numeric_limits<SimTime>::max() - constraint.delay) {
      throw std::overflow_error("timing constraint overflows SimTime");
    }
    earliest = std::max(earliest, preceding_time + constraint.delay);
  }
  return earliest;
}

void HbmTimingEngine::record(HbmCommand command, const HbmAddress& address,
                             SimTime when) {
  command = timing_command(command);
  for (const auto scope : {TimingScope::Bank, TimingScope::BankGroup,
                           TimingScope::Sid,
                           TimingScope::PseudoChannel, TimingScope::Channel}) {
    auto& history = history_[resource_key(scope, address)][command_index(command)];
    history.push_back(when);
    if (history.size() > 4) history.pop_front();
  }
}

}  // namespace hbmsim
