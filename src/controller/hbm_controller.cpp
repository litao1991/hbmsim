#include "hbmsim/controller/hbm_controller.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace hbmsim {
namespace {

bool is_data_command(HbmCommand command) {
  return command == HbmCommand::Read || command == HbmCommand::Write ||
         command == HbmCommand::ReadAuto || command == HbmCommand::WriteAuto;
}

DramCommand to_dram_command(HbmCommand command) {
  switch (command) {
    case HbmCommand::Act: return DramCommand::Activate;
    case HbmCommand::PreBank: return DramCommand::PrechargeBank;
    case HbmCommand::PreAll: return DramCommand::PrechargeAll;
    case HbmCommand::Read: return DramCommand::Read;
    case HbmCommand::Write: return DramCommand::Write;
    case HbmCommand::ReadAuto: return DramCommand::ReadAuto;
    case HbmCommand::WriteAuto: return DramCommand::WriteAuto;
    case HbmCommand::RefreshAllBank: return DramCommand::RefreshAllBank;
    case HbmCommand::RefreshPerBank: return DramCommand::RefreshPerBank;
    case HbmCommand::RfmAllBank: return DramCommand::RfmAllBank;
    case HbmCommand::RfmPerBank: return DramCommand::RfmPerBank;
    case HbmCommand::Count: break;
  }
  throw std::logic_error("unknown HBM command");
}

HbmCommand to_hbm_command(DramCommand command) {
  switch (command) {
    case DramCommand::Activate: return HbmCommand::Act;
    case DramCommand::PrechargeBank: return HbmCommand::PreBank;
    case DramCommand::PrechargeAll: return HbmCommand::PreAll;
    case DramCommand::Read: return HbmCommand::Read;
    case DramCommand::Write: return HbmCommand::Write;
    case DramCommand::ReadAuto: return HbmCommand::ReadAuto;
    case DramCommand::WriteAuto: return HbmCommand::WriteAuto;
    case DramCommand::RefreshAllBank: return HbmCommand::RefreshAllBank;
    case DramCommand::RefreshPerBank: return HbmCommand::RefreshPerBank;
    case DramCommand::RfmAllBank: return HbmCommand::RfmAllBank;
    case DramCommand::RfmPerBank: return HbmCommand::RfmPerBank;
  }
  throw std::logic_error("unknown generic DRAM command");
}

bool has_capacity(std::size_t capacity, std::size_t used,
                  std::size_t requested) {
  return capacity == 0 || (used <= capacity && requested <= capacity - used);
}

SimTime maintenance_duration(HbmCommand command,
                             const HbmTimingSpec& timing) {
  switch (command) {
    case HbmCommand::RefreshAllBank: return timing.t_rfc;
    case HbmCommand::RefreshPerBank: return timing.t_rfcpb;
    case HbmCommand::RfmAllBank: return timing.t_rfmab;
    case HbmCommand::RfmPerBank: return timing.t_rfmpb;
    default: return 0;
  }
}

}  // namespace

HbmController::HbmController(HbmControllerConfig config,
                             const IHbmAddressMapper& mapper, HbmStats& stats)
    : config_(std::move(config)),
      mapper_(mapper),
      stats_(stats),
      timing_engine_(config_.timing),
      command_planner_(config_.standard.get()),
      scheduler_(scheduler_for(config_.scheduler)),
      row_policy_(make_row_policy(config_.row_policy)),
      refresh_manager_(make_refresh_manager(config_.refresh_policy)),
      banks_(config_.bank_count),
      data_bus_ready_at_(config_.pseudo_channel_count, 0) {}

bool HbmController::can_reserve(std::size_t reads, std::size_t writes) const {
  return has_capacity(config_.read_queue_capacity,
                      read_queue_.size() + reserved_reads_, reads) &&
         has_capacity(config_.write_queue_capacity,
                      write_queue_.size() + reserved_writes_, writes);
}

void HbmController::reserve(std::size_t reads, std::size_t writes) {
  if (!can_reserve(reads, writes)) {
    throw std::logic_error("controller reservation overflow");
  }
  reserved_reads_ += reads;
  reserved_writes_ += writes;
}

void HbmController::cancel_reservation(std::size_t reads,
                                       std::size_t writes) {
  if (reads > reserved_reads_ || writes > reserved_writes_) {
    throw std::logic_error("controller reservation underflow");
  }
  reserved_reads_ -= reads;
  reserved_writes_ -= writes;
}

void HbmController::admit_reserved(HbmAccess access, SimTime now) {
  prepare_refresh_for_arrival(now);
  access.enqueued_at = now;
  if (access.op == HbmOp::Read) {
    if (reserved_reads_ == 0) throw std::logic_error("missing read reservation");
    --reserved_reads_;
    read_queue_.push_back(std::move(access));
  } else {
    if (reserved_writes_ == 0) throw std::logic_error("missing write reservation");
    --reserved_writes_;
    write_queue_.push_back(std::move(access));
  }
  ++outstanding_accesses_;
  auto& queue = stats_.channels.at(config_.channel).queue;
  queue.max_read_depth = std::max<std::uint64_t>(queue.max_read_depth,
                                                 read_queue_.size());
  queue.max_write_depth = std::max<std::uint64_t>(queue.max_write_depth,
                                                   write_queue_.size());
}

bool HbmController::mark_wakeup(SimTime when) {
  if (wakeup_at_.has_value() && *wakeup_at_ <= when) return false;
  wakeup_at_ = when;
  return true;
}

void HbmController::clear_wakeup(SimTime when) {
  if (wakeup_at_.has_value() && *wakeup_at_ == when) wakeup_at_.reset();
}

std::optional<SimTime> HbmController::claim_refresh_event() {
  if (!next_refresh_due_.has_value()) return std::nullopt;
  if (refresh_due_at_.has_value() && *refresh_due_at_ == *next_refresh_due_) {
    return std::nullopt;
  }
  refresh_due_at_ = next_refresh_due_;
  return refresh_due_at_;
}

std::size_t HbmController::local_bank(std::uint32_t flat_bank) const {
  if (flat_bank < config_.first_flat_bank ||
      flat_bank >= config_.first_flat_bank + config_.bank_count) {
    throw std::out_of_range("address does not belong to controller");
  }
  return flat_bank - config_.first_flat_bank;
}

void HbmController::update_write_drain() {
  if (draining_writes_ &&
      write_queue_.size() <= config_.write_drain_low_watermark) {
    draining_writes_ = false;
  } else if (!draining_writes_ && !write_queue_.empty() &&
             write_queue_.size() >= config_.write_drain_high_watermark) {
    draining_writes_ = true;
  }
}

void HbmController::update_starvation(SimTime now) {
  if (config_.starvation_threshold == 0) return;
  auto update = [&](std::deque<HbmAccess>& queue) {
    for (auto& access : queue) {
      if (!access.starvation_reported && now >= access.enqueued_at &&
          now - access.enqueued_at >= config_.starvation_threshold) {
        access.starvation_reported = true;
        ++stats_.channels.at(config_.channel).queue.starvation_events;
      }
    }
  };
  update(read_queue_);
  update(write_queue_);
}

std::optional<HbmController::Candidate> HbmController::choose_next(
    SimTime now) {
  const bool prefer_writes = draining_writes_;
  auto& primary = prefer_writes ? write_queue_ : read_queue_;
  auto& fallback = prefer_writes ? read_queue_ : write_queue_;
  const bool primary_is_write = prefer_writes;
  auto& queue = primary.empty() ? fallback : primary;
  const bool is_write = primary.empty() ? !primary_is_write : primary_is_write;
  std::vector<SchedulerCandidate> candidates;
  candidates.reserve(queue.size());
  for (std::size_t index = 0; index < queue.size(); ++index) {
    auto& access = queue[index];
    const auto& bank = banks_.at(local_bank(access.address.flat_bank));
    const auto command = command_planner_.next(
        access.op, access.address.row, bank,
        config_.standard && row_policy_->use_auto_precharge());
    const bool data = is_data_command(command);
    const auto ready = earliest_command(command, access.address, now);
    if (ready <= now && !access.command_eligible) {
      access.command_eligible = true;
      access.command_eligible_since = now;
    } else if (ready > now) {
      access.command_eligible = false;
    }
    candidates.push_back({index, is_write, to_dram_command(command), ready,
                          access.sequence, data && !access.activated, data});
  }
  const auto selected = scheduler_.choose(candidates, now);
  if (!selected.has_value()) return std::nullopt;
  Candidate candidate{selected->is_write, selected->queue_index,
                      to_hbm_command(selected->command), selected->ready_at,
                      selected->row_hit ? 3 : selected->data_command ? 2 : 1};
  if (candidate.ready_at > now || candidate.command != HbmCommand::PreBank) {
    return candidate;
  }

  const auto& pre_queue = candidate.is_write ? write_queue_ : read_queue_;
  const auto target_bank = pre_queue[candidate.index].address.flat_bank;
  std::optional<Candidate> waiting_data;
  const auto find_waiting = [&](const std::deque<HbmAccess>& source,
                                bool source_is_write) {
    for (std::size_t index = 0; index < source.size(); ++index) {
      const auto& access = source[index];
      if (access.address.flat_bank != target_bank) continue;
      const auto& bank = banks_.at(local_bank(access.address.flat_bank));
      const auto command = command_planner_.next(
          access.op, access.address.row, bank,
          config_.standard && row_policy_->use_auto_precharge());
      if (!is_data_command(command)) continue;
      const auto ready = earliest_command(command, access.address, now);
      Candidate current{source_is_write, index, command, ready, 2};
      const auto older = !waiting_data.has_value() ||
          access.sequence < (waiting_data->is_write ? write_queue_ : read_queue_)
                                [waiting_data->index]
                                    .sequence;
      if (!waiting_data.has_value() || ready < waiting_data->ready_at ||
          (ready == waiting_data->ready_at && older)) {
        waiting_data = current;
      }
    }
  };
  find_waiting(read_queue_, false);
  find_waiting(write_queue_, true);
  return waiting_data.has_value() ? waiting_data
                                  : std::optional<Candidate>{candidate};
}

std::optional<HbmController::MaintenanceCandidate>
HbmController::choose_maintenance(SimTime now) const {
  const auto any_open = std::any_of(
      banks_.begin(), banks_.end(),
      [](const HbmBankState& bank) { return bank.open_row.has_value(); });
  if (refresh_pending_) {
    const auto requested = refresh_manager_->all_bank_command();
    const auto decision = command_planner_.next(
        requested, 0, banks_.front(), any_open);
    const auto address = mapper_.bank_address(config_.first_flat_bank);
    return MaintenanceCandidate{requested, decision.command, 0,
                                earliest_command(decision.command, address, now),
                                decision.final_command};
  }
  std::optional<MaintenanceCandidate> earliest;
  for (std::uint32_t index = 0; index < banks_.size(); ++index) {
    const auto& bank = banks_[index];
    HbmCommand requested;
    if (bank.rfm_pending) {
      requested = HbmCommand::RfmPerBank;
    } else if (bank.refresh_pending) {
      requested = HbmCommand::RefreshPerBank;
    }
    else continue;
    const auto address = mapper_.bank_address(config_.first_flat_bank + index);
    const auto decision = command_planner_.next(
        requested, 0, bank, any_open);
    MaintenanceCandidate candidate{
        requested, decision.command, index,
        earliest_command(decision.command, address, now),
        decision.final_command};
    if (!earliest.has_value() || candidate.ready_at < earliest->ready_at ||
        (candidate.ready_at == earliest->ready_at &&
         index < earliest->local_bank)) {
      earliest = candidate;
    }
  }
  return earliest;
}

SimTime HbmController::earliest_all_bank(HbmCommand command,
                                         SimTime now) const {
  SimTime ready = now;
  for (std::uint32_t index = 0; index < banks_.size(); ++index) {
    ready = std::max(ready, timing_engine_.earliest_issue(
                                command,
                                mapper_.bank_address(config_.first_flat_bank + index),
                                now));
  }
  return ready;
}

SimTime HbmController::command_bus_ready(HbmCommand command) const {
  if (!config_.standard) return unified_command_bus_ready_at_;
  switch (config_.standard->command_bus(command)) {
    case CommandBus::Unified: return unified_command_bus_ready_at_;
    case CommandBus::Row: return row_command_bus_ready_at_;
    case CommandBus::Column: return column_command_bus_ready_at_;
  }
  throw std::logic_error("unknown command bus");
}

SimTime HbmController::earliest_command(HbmCommand command,
                                        const HbmAddress& address,
                                        SimTime now) const {
  const auto timing_ready = config_.standard &&
                                    config_.standard->transition_for(command).scope ==
                                        CommandScope::Channel
                                ? earliest_all_bank(command, now)
                                : timing_engine_.earliest_issue(command, address, now);
  return std::max(timing_ready, command_bus_ready(command));
}

void HbmController::apply_transition(HbmCommand command,
                                     std::uint32_t target_row,
                                     std::size_t bank_index) {
  if (!config_.standard) {
    if (command == HbmCommand::PreAll ||
        command == HbmCommand::RefreshAllBank ||
        command == HbmCommand::RfmAllBank) {
      for (auto& bank : banks_) bank.open_row.reset();
    } else {
      auto& bank = banks_.at(bank_index);
      if (command == HbmCommand::Act) bank.open_row = target_row;
      else if (command == HbmCommand::PreBank ||
               command == HbmCommand::RefreshPerBank ||
               command == HbmCommand::RfmPerBank)
        bank.open_row.reset();
    }
    return;
  }
  const auto& transition = config_.standard->transition_for(command);
  if (transition.scope == CommandScope::Channel) {
    for (auto& bank : banks_) {
      config_.standard->apply_transition(command, target_row, bank);
    }
  } else {
    config_.standard->apply_transition(command, target_row,
                                       banks_.at(bank_index));
  }
}

void HbmController::record_command(HbmCommand command,
                                   const HbmAddress& address,
                                   std::size_t bank_index, SimTime now) {
  if (config_.standard && !config_.standard->supports(command)) {
    throw std::logic_error("controller attempted a command unsupported by standard");
  }
  timing_engine_.record(command, address, now);
  ++stats_.issued_commands;
  auto& channel = stats_.channels.at(config_.channel);
  ++channel.command_bus.issued_commands;
  ++channel.banks.at(bank_index).issued_commands;
  const auto duration = config_.standard
                            ? config_.standard->command_duration(command)
                            : config_.timing.t_command;
  channel.command_bus.busy_time += duration;
  const auto ready = now + duration;
  if (!config_.standard ||
      config_.standard->command_bus(command) == CommandBus::Unified) {
    unified_command_bus_ready_at_ = ready;
  } else if (config_.standard->command_bus(command) == CommandBus::Row) {
    row_command_bus_ready_at_ = ready;
    channel.row_command_bus.busy_time += duration;
    ++channel.row_command_bus.issued_commands;
  } else {
    column_command_bus_ready_at_ = ready;
    channel.column_command_bus.busy_time += duration;
    ++channel.column_command_bus.issued_commands;
  }
  switch (command) {
    case HbmCommand::Act: ++stats_.act_commands; break;
    case HbmCommand::PreBank:
    case HbmCommand::PreAll: ++stats_.pre_commands; break;
    case HbmCommand::Read:
    case HbmCommand::ReadAuto: ++stats_.read_commands; break;
    case HbmCommand::Write:
    case HbmCommand::WriteAuto: ++stats_.write_commands; break;
    default: break;
  }
}

std::optional<HbmIssuedAccess> HbmController::issue(Candidate candidate,
                                                     SimTime now) {
  auto& queue = candidate.is_write ? write_queue_ : read_queue_;
  if (candidate.index >= queue.size()) throw std::logic_error("stale scheduler candidate");
  auto& access = queue[candidate.index];
  const auto bank_index = local_bank(access.address.flat_bank);
  auto& bank = banks_.at(bank_index);
  if (!access.first_command_issued) {
    access.first_command_issued = true;
    access.first_command_at = now;
    access.latency_breakdown.queue_wait = now - access.enqueued_at;
    stats_.channels.at(config_.channel).queue.queue_wait_time +=
        access.latency_breakdown.queue_wait;
  }
  access.command_eligible = false;
  record_command(candidate.command, access.address, bank_index, now);
  if (candidate.command == HbmCommand::Act) {
    apply_transition(candidate.command, access.address.row, bank_index);
    ++bank.activation_count;
    if (config_.enable_rfm &&
        bank.activation_count >= config_.rfm_activation_threshold) {
      bank.rfm_pending = true;
      bank.activation_count = 0;
    }
    access.activated = true;
    return std::nullopt;
  }
  if (candidate.command == HbmCommand::PreBank ||
      candidate.command == HbmCommand::PreAll) {
    if (bank.open_row.has_value() && *bank.open_row != access.address.row) {
      access.access_class = HbmAccessClass::RowConflict;
    }
    apply_transition(candidate.command, access.address.row, bank_index);
    return std::nullopt;
  }
  if (!bank.open_row.has_value() || *bank.open_row != access.address.row) {
    throw std::logic_error("data command issued without target row open");
  }
  if (!access.activated && access.access_class != HbmAccessClass::RowConflict) {
    access.access_class = HbmAccessClass::RowHit;
  }
  const auto completed_address = access.address;
  const auto completed_size = access.size_bytes;
  const auto completed_class = access.access_class;
  std::vector<HbmAccess> completed;
  completed.push_back(access);
  queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(candidate.index));
  if (config_.enable_request_merging) {
    for (auto iterator = queue.begin(); iterator != queue.end();) {
      const auto& other = iterator->address;
      const bool same_location =
          other.flat_bank == completed_address.flat_bank &&
          other.row == completed_address.row &&
          other.column == completed_address.column &&
          iterator->size_bytes == completed_size;
      if (!same_location) {
        ++iterator;
        continue;
      }
      iterator->access_class = completed_class;
      completed.push_back(*iterator);
      iterator = queue.erase(iterator);
      ++stats_.channels.at(config_.channel).queue.merged_accesses;
    }
  }
  if (config_.standard) {
    apply_transition(candidate.command, completed_address.row, bank_index);
  } else if (row_policy_->use_auto_precharge()) {
    bank.precharge_pending = true;
  }

  auto& channel = stats_.channels.at(config_.channel);
  const auto cas_latency = candidate.is_write &&
                                   config_.timing.use_extended_hbm_timing
                               ? config_.timing.t_cwl
                               : config_.timing.t_cl;
  const auto data_ready = now + cas_latency;
  auto& bus_ready = data_bus_ready_at_.at(completed_address.pseudo_channel);
  const auto data_start = std::max(data_ready, bus_ready);
  const auto duration = config_.data_rate.transfer_time(completed_size);
  const auto completion = data_start + duration;
  bus_ready = completion;
  channel.data_bus_busy_time += duration;
  channel.pseudo_channels.at(completed_address.pseudo_channel).busy_time += duration;
  for (auto& item : completed) {
    if (!item.first_command_issued) {
      item.first_command_issued = true;
      item.first_command_at = now;
      item.latency_breakdown.queue_wait = now - item.enqueued_at;
      channel.queue.queue_wait_time += item.latency_breakdown.queue_wait;
    }
    item.latency_breakdown.command_phase = now - item.first_command_at;
    item.latency_breakdown.data_ready = cas_latency;
    item.latency_breakdown.data_bus_wait = data_start - data_ready;
    item.latency_breakdown.data_service = duration;
    channel.queue.command_wait_time += item.latency_breakdown.command_phase;
    channel.queue.array_wait_time += item.latency_breakdown.data_ready;
    channel.queue.data_bus_wait_time += item.latency_breakdown.data_bus_wait;
    channel.queue.data_service_time += item.latency_breakdown.data_service;
  }
  channel.banks.at(bank_index).busy_time +=
      completion - completed.front().first_command_at;
  return HbmIssuedAccess{std::move(completed), completion};
}

void HbmController::issue_maintenance(MaintenanceCandidate candidate,
                                      SimTime now) {
  const auto address =
      mapper_.bank_address(config_.first_flat_bank + candidate.local_bank);
  record_command(candidate.command, address, candidate.local_bank, now);
  apply_transition(candidate.command, 0, candidate.local_bank);
  if (!candidate.final_command) return;

  auto& channel = stats_.channels.at(config_.channel);
  switch (candidate.requested) {
    case HbmCommand::RefreshAllBank:
      refresh_pending_ = false;
      ++channel.refreshes;
      for (auto& bank : banks_) {
        bank.refresh_pending = false;
        bank.activation_count = 0;
      }
      break;
    case HbmCommand::RefreshPerBank:
      banks_.at(candidate.local_bank).refresh_pending = false;
      banks_.at(candidate.local_bank).activation_count = 0;
      ++channel.per_bank_refreshes;
      break;
    case HbmCommand::RfmAllBank:
      refresh_pending_ = false;
      ++stats_.rfm_events;
      ++channel.rfm_events;
      for (auto& bank : banks_) {
        bank.rfm_pending = false;
        bank.activation_count = 0;
      }
      break;
    case HbmCommand::RfmPerBank:
      banks_.at(candidate.local_bank).rfm_pending = false;
      banks_.at(candidate.local_bank).activation_count = 0;
      ++stats_.rfm_events;
      ++channel.rfm_events;
      break;
    default: throw std::logic_error("invalid maintenance final command");
  }
  refresh_busy_until_ = now + maintenance_duration(candidate.requested,
                                                    config_.timing);
}

void HbmController::apply_idle_refresh(SimTime when) {
  if (refresh_manager_->uses_all_bank_refresh()) {
    refresh_pending_ = true;
  } else {
    banks_.at(next_per_bank_refresh_).refresh_pending = true;
    next_per_bank_refresh_ = (next_per_bank_refresh_ + 1) % banks_.size();
  }
  auto pending = [&] {
    return refresh_pending_ || std::any_of(
                                   banks_.begin(), banks_.end(),
                                   [](const HbmBankState& bank) {
                                     return bank.refresh_pending;
                                   });
  };
  SimTime cursor = std::max(when, refresh_busy_until_);
  while (pending()) {
    const auto candidate = choose_maintenance(cursor);
    if (!candidate.has_value()) {
      throw std::logic_error("refresh pending without maintenance candidate");
    }
    cursor = std::max(cursor, candidate->ready_at);
    issue_maintenance(*candidate, cursor);
    cursor = std::max(cursor, command_bus_ready(candidate->command));
  }
}

void HbmController::prepare_refresh_for_arrival(SimTime now) {
  if (config_.refresh_interval == 0) return;
  if (!next_refresh_due_.has_value()) {
    next_refresh_due_ = now + config_.refresh_interval;
  }
  while (*next_refresh_due_ <= now) {
    apply_idle_refresh(*next_refresh_due_);
    *next_refresh_due_ += config_.refresh_interval;
  }
}

void HbmController::handle_refresh_event(SimTime now) {
  if (!refresh_due_at_.has_value() || *refresh_due_at_ != now) return;
  refresh_due_at_.reset();
  if (!next_refresh_due_.has_value() || *next_refresh_due_ != now) return;
  next_refresh_due_ = now + config_.refresh_interval;
  if (outstanding_accesses_ == 0) {
    apply_idle_refresh(now);
    return;
  }
  if (refresh_manager_->uses_all_bank_refresh()) {
    refresh_pending_ = true;
  } else {
    banks_.at(next_per_bank_refresh_).refresh_pending = true;
    next_per_bank_refresh_ = (next_per_bank_refresh_ + 1) % banks_.size();
  }
}

HbmControllerStep HbmController::drive(SimTime now) {
  update_starvation(now);
  if (now < refresh_busy_until_) {
    const auto unaccounted_from = std::max(now, refresh_stall_accounted_until_);
    if (refresh_busy_until_ > unaccounted_from) {
      stats_.channels.at(config_.channel).queue.refresh_stall_time +=
          refresh_busy_until_ - unaccounted_from;
      refresh_stall_accounted_until_ = refresh_busy_until_;
    }
    return {refresh_busy_until_, std::nullopt};
  }
  if (const auto maintenance = choose_maintenance(now); maintenance.has_value()) {
    if (maintenance->ready_at > now) return {maintenance->ready_at, std::nullopt};
    issue_maintenance(*maintenance, now);
    return {maintenance->final_command ? refresh_busy_until_ : now,
            std::nullopt};
  }
  update_write_drain();
  const auto candidate = choose_next(now);
  if (!candidate.has_value()) return {};
  if (candidate->ready_at > now) return {candidate->ready_at, std::nullopt};
  const auto completed = issue(*candidate, now);
  return {now, completed};
}

void HbmController::complete_access(const HbmAccess& access, SimTime) {
  if (outstanding_accesses_ == 0) throw std::logic_error("access completion underflow");
  --outstanding_accesses_;
  auto& channel = stats_.channels.at(config_.channel);
  channel.completed_bytes += access.size_bytes;
  switch (access.access_class) {
    case HbmAccessClass::RowHit: ++stats_.row_hits; break;
    case HbmAccessClass::RowClosed: ++stats_.row_closed; break;
    case HbmAccessClass::RowConflict: ++stats_.row_conflicts; break;
  }
}

}  // namespace hbmsim
