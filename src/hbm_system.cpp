#include "hbmsim/hbm_system.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hbmsim {

namespace {

bool is_data_command(HbmCommand command) {
  return command == HbmCommand::Read || command == HbmCommand::Write;
}

}  // namespace

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
  if (refresh_interval != 0 && refresh_interval < timing.t_rfc) {
    throw std::invalid_argument("refresh interval must be at least tRFC");
  }
}

HbmSystem::HbmSystem(HbmConfig config)
    : config_(config),
      address_mapper_(config.topology, config.address_interleave_bytes,
                      config.columns_per_row, config.rows_per_bank),
      timing_engine_(config.timing) {
  config_.validate();
  channels_.resize(config_.topology.channel_count());
  banks_.resize(address_mapper_.bank_count());
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
  const auto granularity = config_.simulation_access_granularity_bytes == 0
                               ? transaction.size_bytes
                               : config_.simulation_access_granularity_bytes;
  std::vector<Access> accesses;
  auto address = transaction.address;
  auto remaining = transaction.size_bytes;
  while (remaining != 0) {
    const auto bytes = std::min(remaining, granularity);
    accesses.push_back(Access{transaction.id, transaction.op, address_mapper_.map(address),
                              bytes, transaction.arrival_time, transaction.client, 0,
                              HbmAccessClass::RowClosed});
    remaining -= bytes;
    if (remaining != 0) {
      if (address > std::numeric_limits<std::uint64_t>::max() - bytes) {
        throw std::overflow_error("transaction address range overflows uint64_t");
      }
      address += bytes;
    }
  }
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
    if (access.op == HbmOp::Read) state.read_queue.push_back(access);
    else state.write_queue.push_back(access);
    touched_channels.insert(channel);
  }
  for (const auto channel : touched_channels) {
    schedule_controller_wake(channel, event_queue_.now());
    if (config_.refresh_interval != 0 && !channels_[channel].refresh_due_at.has_value()) {
      const auto due = event_queue_.now() + config_.refresh_interval;
      channels_[channel].refresh_due_at = due;
      event_queue_.schedule(due, EventType::RefreshDue,
                            [this, channel] { refresh_due(channel); });
    }
  }
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
  const auto& state = channels_.at(channel);
  const auto choose_from = [&](const std::deque<Access>& queue, bool is_write,
                               std::optional<Candidate>& best_ready,
                               std::optional<Candidate>& earliest) {
    for (std::size_t index = 0; index < queue.size(); ++index) {
      const auto& access = queue[index];
      const auto& bank = banks_.at(access.address.flat_bank);
      const auto command = command_planner_.next(access.op, access.address.row, bank);
      const auto ready = timing_engine_.earliest_issue(command, access.address, now);
      const auto priority = is_data_command(command) && !access.activated ? 3 :
                            is_data_command(command) ? 2 : 1;
      Candidate candidate{is_write, index, command, ready, priority};
      const auto older_than = [&](const Candidate& other) {
        const auto& other_queue = other.is_write ? state.write_queue : state.read_queue;
        return access.sequence < other_queue[other.index].sequence;
      };
      if (ready <= now) {
        if (!best_ready.has_value() || candidate.priority > best_ready->priority ||
            (candidate.priority == best_ready->priority && older_than(*best_ready))) {
          best_ready = candidate;
        }
      } else if (!earliest.has_value() || ready < earliest->ready_at ||
                 (ready == earliest->ready_at && candidate.priority > earliest->priority) ||
                 (ready == earliest->ready_at && candidate.priority == earliest->priority &&
                  older_than(*earliest))) {
        earliest = candidate;
      }
    }
  };

  const bool prefer_writes = state.draining_writes;
  const auto& primary = prefer_writes ? state.write_queue : state.read_queue;
  const auto& fallback = prefer_writes ? state.read_queue : state.write_queue;
  const bool primary_is_write = prefer_writes;
  std::optional<Candidate> best_ready;
  std::optional<Candidate> earliest;
  if (!primary.empty()) choose_from(primary, primary_is_write, best_ready, earliest);
  else choose_from(fallback, !primary_is_write, best_ready, earliest);
  return best_ready.has_value() ? best_ready : earliest;
}

void HbmSystem::drive_controller(std::uint32_t channel) {
  auto& state = channels_.at(channel);
  const auto now = event_queue_.now();
  if (now < state.refresh_busy_until) {
    schedule_controller_wake(channel, state.refresh_busy_until);
    return;
  }
  if (state.refresh_pending) {
    HbmAddress refresh_address;
    refresh_address.flat_channel = channel;
    timing_engine_.record(HbmCommand::RefreshAllBank, refresh_address, now);
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
    state.refresh_busy_until = now + config_.timing.t_rfc;
    schedule_controller_wake(channel, state.refresh_busy_until);
    return;
  }
  if (state.draining_writes && state.write_queue.size() <= config_.write_drain_low_watermark) {
    state.draining_writes = false;
  } else if (!state.draining_writes && !state.write_queue.empty() &&
             state.write_queue.size() >= config_.write_drain_high_watermark) {
    state.draining_writes = true;
  }
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

  if (candidate.command == HbmCommand::Act) {
    bank.open_row = queued_access.address.row;
    bank.precharge_pending = false;
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
  if (config_.row_policy == RowPolicy::Closed) bank.precharge_pending = true;

  if (now > std::numeric_limits<SimTime>::max() - config_.timing.t_cl) {
    throw std::overflow_error("CAS latency overflows SimTime");
  }
  const auto data_ready = now + config_.timing.t_cl;
  const auto data_start = std::max(data_ready, state.data_bus_ready_at);
  const auto data_duration = transfer_time(access.size_bytes);
  if (data_start > std::numeric_limits<SimTime>::max() - data_duration) {
    throw std::overflow_error("access completion overflows SimTime");
  }
  const auto completion_time = data_start + data_duration;
  state.data_bus_ready_at = completion_time;
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

void HbmSystem::finish_access(const Access& access, std::uint32_t channel,
                              SimTime completion_time) {
  switch (access.access_class) {
    case HbmAccessClass::RowHit: ++stats_.row_hits; break;
    case HbmAccessClass::RowClosed: ++stats_.row_closed; break;
    case HbmAccessClass::RowConflict: ++stats_.row_conflicts; break;
  }
  stats_.channels[channel].completed_bytes += access.size_bytes;
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
  state.refresh_due_at.reset();
  if (parents_.empty()) return;
  state.refresh_pending = true;
  schedule_controller_wake(channel, event_queue_.now());
  if (config_.refresh_interval != 0) {
    const auto due = event_queue_.now() + config_.refresh_interval;
    state.refresh_due_at = due;
    event_queue_.schedule(due, EventType::RefreshDue,
                          [this, channel] { refresh_due(channel); });
  }
}

}  // namespace hbmsim
