#include "hbmsim/hbm_system.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hbmsim {

void HbmConfig::validate() const {
  topology.validate();
  if (channel_bandwidth_bytes_per_ns == 0 || address_interleave_bytes == 0 ||
      columns_per_row == 0 || rows_per_bank == 0 || physical_burst_bytes == 0) {
    throw std::invalid_argument("HBM geometry, bandwidth, and burst sizes must be non-zero");
  }
  if (write_drain_low_watermark > write_drain_high_watermark) {
    throw std::invalid_argument("write drain low watermark exceeds high watermark");
  }
  if (simulation_access_granularity_bytes != 0 &&
      simulation_access_granularity_bytes < physical_burst_bytes) {
    throw std::invalid_argument("simulation access granularity is below physical burst size");
  }
  if (simulation_access_granularity_bytes != 0 &&
      simulation_access_granularity_bytes % physical_burst_bytes != 0) {
    throw std::invalid_argument("simulation access granularity must be a physical-burst multiple");
  }
  const auto refresh_duration = hbmsim::refresh_duration(refresh_policy, timing);
  if (refresh_interval != 0 && refresh_interval < refresh_duration) {
    throw std::invalid_argument("refresh interval must be at least its refresh duration");
  }
  if (enable_rfm && standard != HbmStandard::Hbm4) {
    throw std::invalid_argument("RFM is currently supported only for HBM4");
  }
  if (enable_rfm && rfm_activation_threshold == 0) {
    throw std::invalid_argument("RFM requires a non-zero activation threshold");
  }
}

HbmConfig HbmConfig::hbm4_8000() {
  HbmConfig config;
  config.standard = HbmStandard::Hbm4;
  config.topology.channels_per_stack = 1;
  config.topology.pseudo_channels_per_channel = 2;
  config.topology.bank_groups_per_pseudo_channel = 2;
  config.topology.banks_per_bank_group = 8;
  config.columns_per_row = 256;
  config.rows_per_bank = 16'384;
  config.timing = HbmTimingSpec::hbm4_8000();
  config.channel_bandwidth_bytes_per_ns = 64;
  config.address_interleave_bytes = 32;
  return config;
}

HbmConfig HbmConfig::hbm2_2000() {
  const auto& profile = Hbm2Standard::profile_2000();
  HbmConfig config;
  config.standard = HbmStandard::Hbm2;
  config.topology = profile.topology;
  config.timing = profile.timing;
  config.columns_per_row = profile.columns_per_row;
  config.rows_per_bank = profile.rows_per_bank;
  config.address_interleave_bytes = profile.address_interleave_bytes;
  config.address_mapping = profile.address_mapping;
  config.physical_burst_bytes = profile.physical_burst_bytes;
  config.channel_bandwidth_bytes_per_ns = profile.pseudo_channel_bandwidth_bytes_per_ns;
  return config;
}

HbmConfig HbmConfig::hbm3_6400() {
  const auto& profile = Hbm3Standard::profile_6400();
  HbmConfig config;
  config.standard = HbmStandard::Hbm3;
  config.topology = profile.topology;
  config.timing = profile.timing;
  config.columns_per_row = profile.columns_per_row;
  config.rows_per_bank = profile.rows_per_bank;
  config.address_interleave_bytes = profile.address_interleave_bytes;
  config.address_mapping = profile.address_mapping;
  config.physical_burst_bytes = profile.physical_burst_bytes;
  config.channel_bandwidth_bytes_per_ns = profile.pseudo_channel_bandwidth_bytes_per_ns;
  return config;
}

HbmSystem::HbmSystem(HbmConfig config)
    : config_(config),
      address_mapper_(std::make_unique<HbmAddressMapper>(
          config.topology, config.address_interleave_bytes,
          config.columns_per_row, config.rows_per_bank, config.address_mapping)),
      timing_engine_(config.timing),
      row_policy_(make_row_policy(config.row_policy)),
      refresh_manager_(make_refresh_manager(config.refresh_policy)),
      controller_(command_planner_, timing_engine_, scheduler_for(config.scheduler)) {
  config_.validate();
  channels_.resize(config_.topology.channel_count());
  for (auto& channel : channels_) {
    channel.data_bus_ready_at.assign(config_.topology.pseudo_channels_per_channel, 0);
  }
  banks_.resize(address_mapper_->bank_count());
  stats_.channels.resize(config_.topology.channel_count());
}

SubmitResult HbmSystem::submit(const HbmTransaction& transaction) {
  if (transaction.id == 0 || transaction.size_bytes == 0) {
    return {SubmitStatus::InvalidArgument, {}, "transaction id and size_bytes must both be non-zero"};
  }
  if (transaction.arrival_time < now()) {
    return {SubmitStatus::ArrivalInPast, {}, "transaction arrival time is earlier than simulator time"};
  }
  if (!known_transaction_ids_.insert(transaction.id).second) {
    return {SubmitStatus::DuplicateId, {}, "transaction id was already submitted"};
  }
  ++stats_.submitted_transactions;
  event_queue_.schedule(transaction.arrival_time, EventType::TransactionArrival,
                        [this, transaction] { admit(transaction); });
  return {SubmitStatus::Accepted, RequestToken{transaction.id}, {}};
}

void HbmSystem::set_completion_callback(CompletionCallback callback) {
  completion_callback_ = std::move(callback);
}

void HbmSystem::run() { event_queue_.run(); }

void HbmSystem::run_until(SimTime until) { event_queue_.run_until(until); }

SimTime HbmSystem::transfer_time(std::uint64_t bytes) const {
  constexpr auto max_time = std::numeric_limits<SimTime>::max();
  if (bytes > max_time / kPicosecondsPerNanosecond) {
    throw std::overflow_error("transfer time overflows SimTime");
  }
  const auto scaled = bytes * kPicosecondsPerNanosecond;
  const auto bandwidth = config_.channel_bandwidth_bytes_per_ns;
  if (scaled > max_time - (bandwidth - 1)) {
    throw std::overflow_error("transfer time overflows SimTime");
  }
  return (scaled + bandwidth - 1) / bandwidth;
}

std::vector<HbmSystem::Access> HbmSystem::split_transaction(
    const HbmTransaction& transaction) const {
  std::vector<Access> accesses;
  auto address = transaction.address;
  auto remaining = transaction.size_bytes;
  const auto max_group_bytes = config_.simulation_access_granularity_bytes == 0
                                   ? config_.physical_burst_bytes
                                   : config_.simulation_access_granularity_bytes;
  std::optional<Access> group;
  HbmAddress previous_address;
  const auto same_resource = [](const HbmAddress& left, const HbmAddress& right) {
    return left.flat_channel == right.flat_channel &&
           left.pseudo_channel == right.pseudo_channel &&
           left.bank_group == right.bank_group && left.bank == right.bank &&
           left.row == right.row;
  };
  while (remaining != 0) {
    const auto offset_in_burst = address % config_.physical_burst_bytes;
    const auto bytes_until_burst_boundary =
        config_.physical_burst_bytes - offset_in_burst;
    const auto bytes_until_mapping_boundary =
        address_mapper_->next_mapping_boundary(address);
    const auto bytes = std::min(
        {remaining, bytes_until_burst_boundary, bytes_until_mapping_boundary});
    const auto mapped = address_mapper_->map(address);
    const auto contiguous_column = !group.has_value() ||
                                   mapped.column == previous_address.column ||
                                   mapped.column == previous_address.column + 1;
    const auto can_extend = group.has_value() &&
                            group->size_bytes + bytes <= max_group_bytes &&
                            same_resource(group->address, mapped) && contiguous_column;
    if (can_extend) {
      group->size_bytes += bytes;
    } else {
      if (group.has_value()) accesses.push_back(*group);
      group = Access{transaction.id, transaction.op, mapped, bytes,
                     transaction.arrival_time, transaction.client, 0,
                     HbmAccessClass::RowClosed};
    }
    previous_address = mapped;
    remaining -= bytes;
    if (remaining != 0) {
      if (address > std::numeric_limits<std::uint64_t>::max() - bytes) {
        throw std::overflow_error("transaction address range overflows uint64_t");
      }
      address += bytes;
    }
  }
  if (group.has_value()) accesses.push_back(*group);
  return accesses;
}

void HbmSystem::admit(const HbmTransaction& transaction) {
  auto accesses = split_transaction(transaction);
  auto [parent_it, inserted] = parents_.emplace(
      transaction.id, ParentRequest{transaction, accesses.size(), 0, 0, HbmAccessClass::RowClosed});
  if (!inserted) throw std::logic_error("duplicate active parent transaction");
  (void)parent_it;
  stats_.modeled_accesses += accesses.size();

  std::unordered_set<std::uint32_t> touched_channels;
  for (auto& access : accesses) {
    access.sequence = next_access_sequence_++;
    const auto channel = access.address.flat_channel;
    auto& state = channels_[channel];
    if (touched_channels.insert(channel).second) {
      prepare_refresh_for_arrival(channel, event_queue_.now());
    }
    if (access.op == HbmOp::Read) state.read_queue.push_back(access);
    else state.write_queue.push_back(access);
    ++state.outstanding_accesses;
  }
  for (const auto channel : touched_channels) {
    schedule_controller_wake(channel, event_queue_.now());
  }
}

void HbmSystem::schedule_refresh_due(std::uint32_t channel) {
  if (config_.refresh_interval == 0) return;
  auto& state = channels_.at(channel);
  if (!state.next_refresh_due.has_value()) return;
  const auto due = *state.next_refresh_due;
  if (state.refresh_due_at.has_value() && *state.refresh_due_at == due) return;
  state.refresh_due_at = due;
  event_queue_.schedule(due, EventType::RefreshDue,
                        [this, channel] { refresh_due(channel); });
}

void HbmSystem::issue_all_bank_refresh(std::uint32_t channel, SimTime now) {
  auto& state = channels_.at(channel);
  HbmAddress refresh_address;
  refresh_address.flat_channel = channel;
  timing_engine_.record(refresh_manager_->all_bank_command(), refresh_address, now);
  ++stats_.issued_commands;
  ++stats_.channels[channel].refreshes;
  const auto banks_per_channel = config_.topology.pseudo_channels_per_channel *
                                 config_.topology.bank_groups_per_pseudo_channel *
                                 config_.topology.banks_per_bank_group;
  const auto first_bank = channel * banks_per_channel;
  for (std::uint32_t index = 0; index < banks_per_channel; ++index) {
    banks_[first_bank + index] = {};
  }
  state.refresh_pending = false;
  state.refresh_busy_until = now + refresh_manager_->duration(config_.timing);
}

void HbmSystem::apply_idle_refresh(std::uint32_t channel, SimTime when) {
  auto& state = channels_.at(channel);
  if (refresh_manager_->uses_all_bank_refresh()) {
    issue_all_bank_refresh(channel, when);
    return;
  }
  const auto banks_per_channel = config_.topology.pseudo_channels_per_channel *
                                 config_.topology.bank_groups_per_pseudo_channel *
                                 config_.topology.banks_per_bank_group;
  const auto first_bank = channel * banks_per_channel;
  const auto flat_bank = first_bank + state.next_per_bank_refresh;
  state.next_per_bank_refresh = (state.next_per_bank_refresh + 1) % banks_per_channel;
  issue_maintenance(channel,
                    {refresh_manager_->per_bank_command(), flat_bank, when}, when);
}

void HbmSystem::prepare_refresh_for_arrival(std::uint32_t channel, SimTime now) {
  if (config_.refresh_interval == 0) return;
  auto& state = channels_.at(channel);
  if (!state.next_refresh_due.has_value()) {
    state.next_refresh_due = now + config_.refresh_interval;
  }
  while (*state.next_refresh_due <= now) {
    apply_idle_refresh(channel, *state.next_refresh_due);
    *state.next_refresh_due += config_.refresh_interval;
  }
  schedule_refresh_due(channel);
}

void HbmSystem::schedule_controller_wake(std::uint32_t channel, SimTime when) {
  auto& state = channels_.at(channel);
  if (when < event_queue_.now()) when = event_queue_.now();
  if (state.wakeup_at.has_value() && *state.wakeup_at <= when) return;
  state.wakeup_at = when;
  event_queue_.schedule(when, EventType::ControllerWakeup, [this, channel] {
    channels_.at(channel).wakeup_at.reset();
    drive_controller(channel);
  });
}

std::optional<HbmSystem::Candidate> HbmSystem::choose_next(std::uint32_t channel,
                                                             SimTime now) const {
  return controller_.choose_next(channels_.at(channel), banks_, now);
}

std::optional<HbmSystem::MaintenanceCandidate> HbmSystem::choose_maintenance(
    std::uint32_t channel, SimTime now) const {
  const auto banks_per_channel = config_.topology.pseudo_channels_per_channel *
                                 config_.topology.bank_groups_per_pseudo_channel *
                                 config_.topology.banks_per_bank_group;
  const auto first_bank = channel * banks_per_channel;
  std::optional<MaintenanceCandidate> earliest;
  for (std::uint32_t offset = 0; offset < banks_per_channel; ++offset) {
    const auto flat_bank = first_bank + offset;
    const auto& bank = banks_[flat_bank];
    HbmCommand command;
    if (bank.rfm_pending) command = HbmCommand::RfmPerBank;
    else if (bank.refresh_pending) command = HbmCommand::RefreshPerBank;
    else continue;
    const auto address = address_mapper_->bank_address(flat_bank);
    const auto ready = timing_engine_.earliest_issue(command, address, now);
    MaintenanceCandidate candidate{command, flat_bank, ready};
    if (!earliest.has_value() || candidate.ready_at < earliest->ready_at ||
        (candidate.ready_at == earliest->ready_at && candidate.bank < earliest->bank)) {
      earliest = candidate;
    }
  }
  return earliest;
}

void HbmSystem::drive_controller(std::uint32_t channel) {
  auto& state = channels_.at(channel);
  const auto now = event_queue_.now();
  if (now < state.refresh_busy_until) {
    schedule_controller_wake(channel, state.refresh_busy_until);
    return;
  }
  if (state.refresh_pending) {
    issue_all_bank_refresh(channel, now);
    schedule_controller_wake(channel, state.refresh_busy_until);
    return;
  }
  if (const auto maintenance = choose_maintenance(channel, now); maintenance.has_value()) {
    if (maintenance->ready_at > now) {
      schedule_controller_wake(channel, maintenance->ready_at);
      return;
    }
    issue_maintenance(channel, *maintenance, now);
    schedule_controller_wake(channel, now);
    return;
  }
  controller_.update_write_drain(state, config_.write_drain_high_watermark,
                                 config_.write_drain_low_watermark);
  const auto candidate = choose_next(channel, now);
  if (!candidate.has_value()) return;
  if (candidate->ready_at > now) {
    schedule_controller_wake(channel, candidate->ready_at);
    return;
  }
  issue(channel, *candidate, now);
  schedule_controller_wake(channel, now);
}

void HbmSystem::issue(std::uint32_t channel, Candidate candidate, SimTime now) {
  auto& state = channels_.at(channel);
  auto& queue = candidate.is_write ? state.write_queue : state.read_queue;
  if (candidate.index >= queue.size()) throw std::logic_error("stale scheduler candidate");
  auto& queued_access = queue[candidate.index];
  auto& bank = banks_.at(queued_access.address.flat_bank);
  timing_engine_.record(candidate.command, queued_access.address, now);
  ++stats_.issued_commands;
  switch (candidate.command) {
    case HbmCommand::Act: ++stats_.act_commands; break;
    case HbmCommand::Pre: ++stats_.pre_commands; break;
    case HbmCommand::Read: ++stats_.read_commands; break;
    case HbmCommand::Write: ++stats_.write_commands; break;
    default: break;
  }

  if (candidate.command == HbmCommand::Act) {
    bank.open_row = queued_access.address.row;
    bank.precharge_pending = false;
    ++bank.activation_count;
    if (config_.enable_rfm &&
        bank.activation_count >= config_.rfm_activation_threshold) {
      bank.rfm_pending = true;
      bank.activation_count = 0;
    }
    queued_access.activated = true;
    return;
  }
  if (candidate.command == HbmCommand::Pre) {
    if (bank.open_row.has_value() && *bank.open_row != queued_access.address.row) {
      queued_access.access_class = HbmAccessClass::RowConflict;
    }
    bank.open_row.reset();
    bank.precharge_pending = false;
    return;
  }

  if (!bank.open_row.has_value() || *bank.open_row != queued_access.address.row) {
    throw std::logic_error("data command issued without its target row open");
  }
  if (!queued_access.activated && queued_access.access_class != HbmAccessClass::RowConflict) {
    queued_access.access_class = HbmAccessClass::RowHit;
  }
  Access access = queued_access;
  queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(candidate.index));
  if (row_policy_->close_after_data_command()) bank.precharge_pending = true;

  if (now > std::numeric_limits<SimTime>::max() - config_.timing.t_cl) {
    throw std::overflow_error("CAS latency overflows SimTime");
  }
  const auto data_ready = now + config_.timing.t_cl;
  auto& data_bus_ready_at = state.data_bus_ready_at.at(queued_access.address.pseudo_channel);
  const auto data_start = std::max(data_ready, data_bus_ready_at);
  const auto data_duration = transfer_time(access.size_bytes);
  if (data_start > std::numeric_limits<SimTime>::max() - data_duration) {
    throw std::overflow_error("access completion overflows SimTime");
  }
  const auto completion_time = data_start + data_duration;
  data_bus_ready_at = completion_time;
  if (stats_.channels[channel].data_bus_busy_time >
      std::numeric_limits<SimTime>::max() - data_duration) {
    throw std::overflow_error("data-bus utilization overflows SimTime");
  }
  stats_.channels[channel].data_bus_busy_time += data_duration;
  event_queue_.schedule(completion_time, EventType::TransactionCompletion,
                        [this, access, channel, completion_time] {
    finish_access(access, channel, completion_time);
  });
}

void HbmSystem::issue_maintenance(std::uint32_t channel,
                                  MaintenanceCandidate candidate, SimTime now) {
  auto& bank = banks_.at(candidate.bank);
  const auto address = address_mapper_->bank_address(candidate.bank);
  timing_engine_.record(candidate.command, address, now);
  ++stats_.issued_commands;
  bank.open_row.reset();
  bank.precharge_pending = false;
  bank.activation_count = 0;
  switch (candidate.command) {
    case HbmCommand::RefreshPerBank:
      bank.refresh_pending = false;
      ++stats_.channels[channel].per_bank_refreshes;
      break;
    case HbmCommand::RfmPerBank:
      bank.rfm_pending = false;
      ++stats_.rfm_events;
      ++stats_.channels[channel].rfm_events;
      break;
    default:
      throw std::logic_error("invalid per-bank maintenance command");
  }
}

void HbmSystem::finish_access(const Access& access, std::uint32_t channel,
                              SimTime completion_time) {
  switch (access.access_class) {
    case HbmAccessClass::RowHit: ++stats_.row_hits; break;
    case HbmAccessClass::RowClosed: ++stats_.row_closed; break;
    case HbmAccessClass::RowConflict: ++stats_.row_conflicts; break;
  }
  stats_.channels[channel].completed_bytes += access.size_bytes;
  auto& state = channels_.at(channel);
  if (state.outstanding_accesses == 0) {
    throw std::logic_error("completed access is not outstanding on its channel");
  }
  --state.outstanding_accesses;
  auto parent = parents_.find(access.parent_id);
  if (parent == parents_.end()) throw std::logic_error("missing access parent");
  auto& request = parent->second;
  if (request.remaining_accesses == 0) throw std::logic_error("completed access underflow");
  --request.remaining_accesses;
  if (completion_time >= request.completion_time) {
    request.completion_time = completion_time;
    request.completion_channel = channel;
    request.last_access_class = access.access_class;
  }
  if (request.remaining_accesses == 0) complete_parent(access.parent_id);
}

void HbmSystem::complete_parent(TransactionId id) {
  const auto iterator = parents_.find(id);
  if (iterator == parents_.end()) throw std::logic_error("missing completion parent");
  const auto& request = iterator->second;
  HbmCompletion completion{request.transaction.id,
                           request.transaction.op,
                           request.transaction.address,
                           request.transaction.size_bytes,
                           request.transaction.client,
                           request.completion_channel,
                           request.last_access_class,
                           request.transaction.arrival_time,
                           request.completion_time,
                           request.completion_time - request.transaction.arrival_time};
  ++stats_.completed_transactions;
  if (completion.op == HbmOp::Read) stats_.read_bytes += completion.size_bytes;
  else stats_.write_bytes += completion.size_bytes;
  completions_.push_back(completion);
  if (completion_callback_) completion_callback_(completion);
  parents_.erase(iterator);
}

void HbmSystem::refresh_due(std::uint32_t channel) {
  auto& state = channels_.at(channel);
  if (!state.refresh_due_at.has_value() || *state.refresh_due_at != event_queue_.now()) return;
  state.refresh_due_at.reset();
  if (!state.next_refresh_due.has_value() || *state.next_refresh_due != event_queue_.now()) {
    return;
  }
  const auto due = *state.next_refresh_due;
  state.next_refresh_due = due + config_.refresh_interval;
  if (state.outstanding_accesses == 0) {
    apply_idle_refresh(channel, due);
    return;
  }
  if (refresh_manager_->uses_all_bank_refresh()) {
    state.refresh_pending = true;
  } else {
    const auto banks_per_channel = config_.topology.pseudo_channels_per_channel *
                                   config_.topology.bank_groups_per_pseudo_channel *
                                   config_.topology.banks_per_bank_group;
    const auto first_bank = channel * banks_per_channel;
    const auto flat_bank = first_bank + state.next_per_bank_refresh;
    banks_[flat_bank].refresh_pending = true;
    state.next_per_bank_refresh = (state.next_per_bank_refresh + 1) % banks_per_channel;
  }
  schedule_controller_wake(channel, event_queue_.now());
  schedule_refresh_due(channel);
}

}  // namespace hbmsim
