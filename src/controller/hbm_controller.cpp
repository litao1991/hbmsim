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
    const auto ready = timing_engine_.earliest_issue(command, access.address, now);
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
      const auto ready = timing_engine_.earliest_issue(command, access.address, now);
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
  std::optional<MaintenanceCandidate> earliest;
  for (std::uint32_t index = 0; index < banks_.size(); ++index) {
    const auto& bank = banks_[index];
    HbmCommand command;
    if (bank.rfm_pending) {
      command = bank.open_row.has_value() ? HbmCommand::PreBank
                                           : HbmCommand::RfmPerBank;
    } else if (bank.refresh_pending) {
      command = bank.open_row.has_value() ? HbmCommand::PreBank
                                           : HbmCommand::RefreshPerBank;
    }
    else continue;
    const auto address = mapper_.bank_address(config_.first_flat_bank + index);
    const auto ready = timing_engine_.earliest_issue(command, address, now);
    MaintenanceCandidate candidate{command, index, ready};
    if (!earliest.has_value() || ready < earliest->ready_at ||
        (ready == earliest->ready_at && index < earliest->local_bank)) {
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
  command_bus_ready_at_ = now + duration;
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
    stats_.channels.at(config_.channel).queue.queue_wait_time +=
        now - access.enqueued_at;
  }
  if (access.command_eligible && now >= access.command_eligible_since) {
    stats_.channels.at(config_.channel).queue.command_wait_time +=
        now - access.command_eligible_since;
  }
  access.command_eligible = false;
  record_command(candidate.command, access.address, bank_index, now);
  if (candidate.command == HbmCommand::Act) {
    if (config_.standard) {
      config_.standard->apply_transition(candidate.command,
                                         access.address.row, bank);
    } else {
      bank.open_row = access.address.row;
      bank.precharge_pending = false;
    }
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
    if (config_.standard) {
      config_.standard->apply_transition(candidate.command,
                                         access.address.row, bank);
    } else {
      bank.open_row.reset();
      bank.precharge_pending = false;
    }
    return std::nullopt;
  }
  if (!bank.open_row.has_value() || *bank.open_row != access.address.row) {
    throw std::logic_error("data command issued without target row open");
  }
  if (!access.activated && access.access_class != HbmAccessClass::RowConflict) {
    access.access_class = HbmAccessClass::RowHit;
  }
  HbmAccess completed = access;
  queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(candidate.index));
  if (config_.standard) {
    config_.standard->apply_transition(candidate.command,
                                       completed.address.row, bank);
  } else if (row_policy_->use_auto_precharge()) {
    bank.precharge_pending = true;
  }

  auto& channel = stats_.channels.at(config_.channel);
  channel.queue.array_wait_time += now - completed.first_command_at;
  const auto data_ready = now + config_.timing.t_cl;
  auto& bus_ready = data_bus_ready_at_.at(completed.address.pseudo_channel);
  const auto data_start = std::max(data_ready, bus_ready);
  channel.queue.data_bus_wait_time += data_start - data_ready;
  const auto duration = config_.data_rate.transfer_time(completed.size_bytes);
  const auto completion = data_start + duration;
  bus_ready = completion;
  channel.data_bus_busy_time += duration;
  channel.pseudo_channels.at(completed.address.pseudo_channel).busy_time += duration;
  channel.banks.at(bank_index).busy_time += completion - completed.first_command_at;
  return HbmIssuedAccess{completed, completion};
}

void HbmController::issue_maintenance(MaintenanceCandidate candidate,
                                      SimTime now) {
  auto& bank = banks_.at(candidate.local_bank);
  const auto address =
      mapper_.bank_address(config_.first_flat_bank + candidate.local_bank);
  record_command(candidate.command, address, candidate.local_bank, now);
  bank.open_row.reset();
  bank.precharge_pending = false;
  bank.activation_count = 0;
  auto& stats = stats_.channels.at(config_.channel);
  switch (candidate.command) {
    case HbmCommand::PreBank:
      if (config_.standard) {
        config_.standard->apply_transition(candidate.command, 0, bank);
      } else {
        bank.open_row.reset();
        bank.precharge_pending = false;
      }
      break;
    case HbmCommand::RefreshPerBank:
      bank.refresh_pending = false;
      ++stats.per_bank_refreshes;
      break;
    case HbmCommand::RfmPerBank:
      bank.rfm_pending = false;
      ++stats_.rfm_events;
      ++stats.rfm_events;
      break;
    default: throw std::logic_error("invalid per-bank maintenance command");
  }
}

void HbmController::issue_all_bank_refresh(SimTime now) {
  HbmAddress address;
  address.flat_channel = config_.channel;
  record_command(refresh_manager_->all_bank_command(), address, 0, now);
  ++stats_.channels.at(config_.channel).refreshes;
  for (auto& bank : banks_) bank = {};
  refresh_pending_ = false;
  refresh_busy_until_ = now + refresh_manager_->duration(config_.timing);
}

void HbmController::apply_idle_refresh(SimTime when) {
  if (refresh_manager_->uses_all_bank_refresh()) {
    const auto opened = std::find_if(
        banks_.begin(), banks_.end(),
        [](const HbmBankState& bank) { return bank.open_row.has_value(); });
    if (opened != banks_.end()) {
      const auto index = static_cast<std::size_t>(opened - banks_.begin());
      when = earliest_all_bank(HbmCommand::PreAll, when);
      record_command(HbmCommand::PreAll,
                     mapper_.bank_address(config_.first_flat_bank + index),
                     index, when);
      for (auto& bank : banks_) {
        if (config_.standard) {
          config_.standard->apply_transition(HbmCommand::PreAll, 0, bank);
        } else {
          bank.open_row.reset();
          bank.precharge_pending = false;
        }
      }
      when += config_.timing.t_rp;
    }
    when = earliest_all_bank(refresh_manager_->all_bank_command(), when);
    issue_all_bank_refresh(when);
    return;
  }
  const auto bank = next_per_bank_refresh_;
  next_per_bank_refresh_ = (next_per_bank_refresh_ + 1) % banks_.size();
  const auto address = mapper_.bank_address(config_.first_flat_bank + bank);
  if (banks_[bank].open_row.has_value()) {
    when = timing_engine_.earliest_issue(HbmCommand::PreBank, address, when);
    issue_maintenance({HbmCommand::PreBank, bank, when}, when);
  }
  when = timing_engine_.earliest_issue(refresh_manager_->per_bank_command(),
                                       address, when);
  issue_maintenance({refresh_manager_->per_bank_command(), bank, when}, when);
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
    stats_.channels.at(config_.channel).queue.refresh_stall_time +=
        refresh_busy_until_ - now;
    return {refresh_busy_until_, std::nullopt};
  }
  if (now < command_bus_ready_at_) return {command_bus_ready_at_, std::nullopt};
  if (refresh_pending_) {
    const auto opened = std::find_if(
        banks_.begin(), banks_.end(),
        [](const HbmBankState& bank) { return bank.open_row.has_value(); });
    if (opened != banks_.end()) {
      const auto index = static_cast<std::size_t>(opened - banks_.begin());
      const auto address =
          mapper_.bank_address(config_.first_flat_bank + index);
      const auto ready = earliest_all_bank(HbmCommand::PreAll, now);
      if (ready > now) return {ready, std::nullopt};
      record_command(HbmCommand::PreAll, address, index, now);
      for (auto& bank : banks_) {
        if (config_.standard) {
          config_.standard->apply_transition(HbmCommand::PreAll, 0, bank);
        } else {
          bank.open_row.reset();
          bank.precharge_pending = false;
        }
      }
      all_bank_refresh_ready_at_ = now + config_.timing.t_rp;
      return {command_bus_ready_at_, std::nullopt};
    }
    if (now < all_bank_refresh_ready_at_) {
      return {all_bank_refresh_ready_at_, std::nullopt};
    }
    const auto ready = earliest_all_bank(refresh_manager_->all_bank_command(), now);
    if (ready > now) return {ready, std::nullopt};
    issue_all_bank_refresh(now);
    return {refresh_busy_until_, std::nullopt};
  }
  if (const auto maintenance = choose_maintenance(now); maintenance.has_value()) {
    if (maintenance->ready_at > now) return {maintenance->ready_at, std::nullopt};
    issue_maintenance(*maintenance, now);
    return {command_bus_ready_at_, std::nullopt};
  }
  update_write_drain();
  const auto candidate = choose_next(now);
  if (!candidate.has_value()) return {};
  if (candidate->ready_at > now) return {candidate->ready_at, std::nullopt};
  const auto completed = issue(*candidate, now);
  return {command_bus_ready_at_, completed};
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
