#include "hbmsim/controller/hbm_controller.h"

namespace hbmsim {
namespace {

bool is_data_command(HbmCommand command) {
  return command == HbmCommand::Read || command == HbmCommand::Write;
}

DramCommand to_dram_command(HbmCommand command) {
  switch (command) {
    case HbmCommand::Act: return DramCommand::Activate;
    case HbmCommand::Pre: return DramCommand::Precharge;
    case HbmCommand::Read: return DramCommand::Read;
    case HbmCommand::Write: return DramCommand::Write;
    case HbmCommand::RefreshAllBank: return DramCommand::RefreshAllBank;
    case HbmCommand::RefreshPerBank: return DramCommand::RefreshPerBank;
    case HbmCommand::RfmAllBank: return DramCommand::RfmAllBank;
    case HbmCommand::RfmPerBank: return DramCommand::RfmPerBank;
  }
}

HbmCommand to_hbm_command(DramCommand command) {
  switch (command) {
    case DramCommand::Activate: return HbmCommand::Act;
    case DramCommand::Precharge: return HbmCommand::Pre;
    case DramCommand::Read: return HbmCommand::Read;
    case DramCommand::Write: return HbmCommand::Write;
    case DramCommand::RefreshAllBank: return HbmCommand::RefreshAllBank;
    case DramCommand::RefreshPerBank: return HbmCommand::RefreshPerBank;
    case DramCommand::RfmAllBank: return HbmCommand::RfmAllBank;
    case DramCommand::RfmPerBank: return HbmCommand::RfmPerBank;
  }
}

}  // namespace

void HbmController::update_write_drain(HbmControllerChannelState& state,
                                       std::size_t high_watermark,
                                       std::size_t low_watermark) const {
  if (state.draining_writes && state.write_queue.size() <= low_watermark) {
    state.draining_writes = false;
  } else if (!state.draining_writes && !state.write_queue.empty() &&
             state.write_queue.size() >= high_watermark) {
    state.draining_writes = true;
  }
}

std::optional<HbmControllerCandidate> HbmController::choose_next(
    const HbmControllerChannelState& state,
    const std::vector<HbmBankState>& banks, SimTime now) const {
  const bool prefer_writes = state.draining_writes;
  const auto& queue = prefer_writes ? state.write_queue : state.read_queue;
  const auto& fallback = prefer_writes ? state.read_queue : state.write_queue;
  const bool is_write = prefer_writes;
  const auto& selected_queue = queue.empty() ? fallback : queue;
  const bool selected_is_write = queue.empty() ? !is_write : is_write;
  std::vector<SchedulerCandidate> candidates;
  candidates.reserve(selected_queue.size());
  for (std::size_t index = 0; index < selected_queue.size(); ++index) {
    const auto& access = selected_queue[index];
    const auto& bank = banks.at(access.address.flat_bank);
    const auto command = command_planner_.next(access.op, access.address.row, bank);
    const bool data = is_data_command(command);
    candidates.push_back({index, selected_is_write, to_dram_command(command),
                          timing_engine_.earliest_issue(command, access.address, now),
                          access.sequence, data && !access.activated, data});
  }
  const auto selected = scheduler_.choose(candidates, now);
  if (!selected.has_value()) return std::nullopt;
  HbmControllerCandidate candidate{
      selected->is_write, selected->queue_index, to_hbm_command(selected->command),
      selected->ready_at,
      selected->row_hit ? 3 : selected->data_command ? 2 : 1};
  if (candidate.ready_at > now || candidate.command != HbmCommand::Pre) {
    return candidate;
  }

  const auto& pre_queue = candidate.is_write ? state.write_queue : state.read_queue;
  const auto target_bank = pre_queue[candidate.index].address.flat_bank;
  std::optional<HbmControllerCandidate> waiting_data;
  const auto find_waiting_data = [&](const std::deque<HbmAccess>& source,
                                     bool source_is_write) {
    for (std::size_t index = 0; index < source.size(); ++index) {
      const auto& access = source[index];
      if (access.address.flat_bank != target_bank) continue;
      const auto& bank = banks.at(access.address.flat_bank);
      const auto command = command_planner_.next(access.op, access.address.row, bank);
      if (!is_data_command(command)) continue;
      const auto ready = timing_engine_.earliest_issue(command, access.address, now);
      HbmControllerCandidate data_candidate{source_is_write, index, command, ready, 2};
      if (!waiting_data.has_value() || data_candidate.ready_at < waiting_data->ready_at ||
          (data_candidate.ready_at == waiting_data->ready_at &&
           access.sequence < (waiting_data->is_write ? state.write_queue : state.read_queue)
                                 [waiting_data->index]
                                     .sequence)) {
        waiting_data = data_candidate;
      }
    }
  };
  find_waiting_data(state.read_queue, false);
  find_waiting_data(state.write_queue, true);
  return waiting_data.has_value() ? waiting_data
                                  : std::optional<HbmControllerCandidate>{candidate};
}

}  // namespace hbmsim
