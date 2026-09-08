#include "hbmsim/hbm_system.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hbmsim {

void HbmConfig::validate() const {
  topology.validate();
  pseudo_channel_rate.validate();
  if (address_interleave_bytes == 0 || columns_per_row == 0 ||
      rows_per_bank == 0 || physical_burst_bytes == 0) {
    throw std::invalid_argument("HBM geometry and burst sizes must be non-zero");
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
    throw std::invalid_argument("simulation access granularity must be a burst multiple");
  }
  const auto duration = refresh_duration(refresh_policy, timing);
  if (refresh_interval != 0 && refresh_interval < duration) {
    throw std::invalid_argument("refresh interval must be at least its duration");
  }
  if (enable_rfm &&
      (!standard || !standard->supports(HbmCommand::RfmPerBank))) {
    throw std::invalid_argument("selected standard does not support RFM");
  }
  if (enable_rfm && rfm_activation_threshold == 0) {
    throw std::invalid_argument("RFM requires a non-zero activation threshold");
  }
}

HbmConfig HbmConfig::hbm4_8000() {
  HbmConfig config;
  config.standard = std::make_shared<Hbm4Standard>();
  const auto& organization = config.standard->organization();
  config.topology = organization.topology;
  config.columns_per_row = organization.columns_per_row;
  config.rows_per_bank = organization.rows_per_bank;
  config.physical_burst_bytes = organization.physical_burst_bytes;
  config.address_interleave_bytes = organization.address_interleave_bytes;
  config.address_mapping = organization.address_mapping;
  config.pseudo_channel_rate = organization.pseudo_channel_rate;
  config.timing = config.standard->timing();
  return config;
}

HbmConfig HbmConfig::hbm2_2000() {
  HbmConfig config;
  config.standard = std::make_shared<Hbm2Standard>();
  const auto& organization = config.standard->organization();
  config.topology = organization.topology;
  config.timing = config.standard->timing();
  config.columns_per_row = organization.columns_per_row;
  config.rows_per_bank = organization.rows_per_bank;
  config.address_interleave_bytes = organization.address_interleave_bytes;
  config.address_mapping = organization.address_mapping;
  config.physical_burst_bytes = organization.physical_burst_bytes;
  config.pseudo_channel_rate = organization.pseudo_channel_rate;
  return config;
}

HbmConfig HbmConfig::hbm3_6400() {
  HbmConfig config;
  config.standard = std::make_shared<Hbm3Standard>();
  const auto& organization = config.standard->organization();
  config.topology = organization.topology;
  config.timing = config.standard->timing();
  config.columns_per_row = organization.columns_per_row;
  config.rows_per_bank = organization.rows_per_bank;
  config.address_interleave_bytes = organization.address_interleave_bytes;
  config.address_mapping = organization.address_mapping;
  config.physical_burst_bytes = organization.physical_burst_bytes;
  config.pseudo_channel_rate = organization.pseudo_channel_rate;
  return config;
}

HbmSystem::HbmSystem(HbmConfig config)
    : config_(std::move(config)) {
  if (config_.standard) {
    const auto& organization = config_.standard->organization();
    config_.topology = organization.topology;
    config_.timing = config_.standard->timing();
    config_.columns_per_row = organization.columns_per_row;
    config_.rows_per_bank = organization.rows_per_bank;
    config_.physical_burst_bytes = organization.physical_burst_bytes;
    config_.address_interleave_bytes = organization.address_interleave_bytes;
    config_.address_mapping = organization.address_mapping;
    config_.pseudo_channel_rate = organization.pseudo_channel_rate;
  }
  config_.validate();
  address_mapper_ = std::make_unique<HbmAddressMapper>(
      config_.topology, config_.address_interleave_bytes,
      config_.columns_per_row, config_.rows_per_bank, config_.address_mapping);
  const auto channel_count = config_.topology.channel_count();
  const auto banks_per_channel = address_mapper_->bank_count() / channel_count;
  stats_.channels.resize(channel_count);
  controllers_.reserve(channel_count);
  for (std::uint32_t channel = 0; channel < channel_count; ++channel) {
    auto& channel_stats = stats_.channels[channel];
    channel_stats.pseudo_channels.resize(
        config_.topology.pseudo_channels_per_channel);
    channel_stats.banks.resize(banks_per_channel);
    HbmControllerConfig controller_config;
    controller_config.channel = channel;
    controller_config.first_flat_bank = channel * banks_per_channel;
    controller_config.bank_count = banks_per_channel;
    controller_config.pseudo_channel_count =
        config_.topology.pseudo_channels_per_channel;
    controller_config.timing = config_.timing;
    controller_config.standard = config_.standard;
    controller_config.data_rate = config_.pseudo_channel_rate;
    controller_config.scheduler = config_.scheduler;
    controller_config.row_policy = config_.row_policy;
    controller_config.refresh_policy = config_.refresh_policy;
    controller_config.write_drain_high_watermark =
        config_.write_drain_high_watermark;
    controller_config.write_drain_low_watermark =
        config_.write_drain_low_watermark;
    controller_config.read_queue_capacity = config_.read_queue_capacity;
    controller_config.write_queue_capacity = config_.write_queue_capacity;
    controller_config.starvation_threshold = config_.starvation_threshold;
    controller_config.refresh_interval = config_.refresh_interval;
    controller_config.enable_rfm = config_.enable_rfm;
    controller_config.rfm_activation_threshold =
        config_.rfm_activation_threshold;
    controllers_.push_back(std::make_unique<HbmController>(
        controller_config, *address_mapper_, stats_));
  }
}

SubmitResult HbmSystem::submit(const HbmTransaction& transaction) {
  if (transaction.id == 0 || transaction.size_bytes == 0) {
    return {SubmitStatus::InvalidArgument, {},
            "transaction id and size_bytes must both be non-zero"};
  }
  if (transaction.arrival_time < now()) {
    return {SubmitStatus::ArrivalInPast, {},
            "transaction arrival time is earlier than simulator time"};
  }
  if (known_transaction_ids_.contains(transaction.id)) {
    return {SubmitStatus::DuplicateId, {}, "transaction id was already submitted"};
  }
  auto accesses = split_transaction(transaction);
  struct Reservation {
    std::size_t reads = 0;
    std::size_t writes = 0;
  };
  std::vector<Reservation> reservations(controllers_.size());
  for (const auto& access : accesses) {
    auto& reservation = reservations.at(access.address.flat_channel);
    if (access.op == HbmOp::Read) ++reservation.reads;
    else ++reservation.writes;
  }
  for (std::size_t channel = 0; channel < controllers_.size(); ++channel) {
    const auto& reservation = reservations[channel];
    if (!controllers_[channel]->can_reserve(reservation.reads,
                                             reservation.writes)) {
      ++stats_.rejected_transactions;
      ++stats_.channels[channel].queue.rejected_transactions;
      return {SubmitStatus::Backpressure, {}, "controller queue capacity exceeded"};
    }
  }
  for (std::size_t channel = 0; channel < controllers_.size(); ++channel) {
    controllers_[channel]->reserve(reservations[channel].reads,
                                   reservations[channel].writes);
  }
  known_transaction_ids_.insert(transaction.id);
  ++stats_.submitted_transactions;
  event_queue_.schedule(
      transaction.arrival_time, EventType::TransactionArrival,
      [this, transaction, accesses = std::move(accesses)]() mutable {
        admit(transaction, std::move(accesses));
      });
  return {SubmitStatus::Accepted, RequestToken{transaction.id}, {}};
}

void HbmSystem::set_completion_callback(CompletionCallback callback) {
  completion_callback_ = std::move(callback);
}

void HbmSystem::run() { event_queue_.run(); }

void HbmSystem::run_until(SimTime until) { event_queue_.run_until(until); }

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
  const auto same_resource = [](const HbmAddress& left,
                                const HbmAddress& right) {
    return left.flat_channel == right.flat_channel &&
           left.pseudo_channel == right.pseudo_channel &&
           left.bank_group == right.bank_group && left.bank == right.bank &&
           left.row == right.row;
  };
  while (remaining != 0) {
    const auto burst_remainder = address % config_.physical_burst_bytes;
    const auto burst_boundary = config_.physical_burst_bytes - burst_remainder;
    const auto mapping_boundary =
        address_mapper_->next_mapping_boundary(address);
    const auto bytes = std::min({remaining, burst_boundary, mapping_boundary});
    const auto mapped = address_mapper_->map(address);
    const auto contiguous_column = !group.has_value() ||
                                   mapped.column == previous_address.column ||
                                   mapped.column == previous_address.column + 1;
    const auto can_extend = group.has_value() &&
                            group->size_bytes + bytes <= max_group_bytes &&
                            same_resource(group->address, mapped) &&
                            contiguous_column;
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

void HbmSystem::admit(const HbmTransaction& transaction,
                      std::vector<Access> accesses) {
  const auto [iterator, inserted] = parents_.emplace(
      transaction.id,
      ParentRequest{transaction, accesses.size(), 0, 0,
                    HbmAccessClass::RowClosed});
  if (!inserted) throw std::logic_error("duplicate active parent transaction");
  (void)iterator;
  stats_.modeled_accesses += accesses.size();
  std::vector<bool> touched(controllers_.size(), false);
  for (auto& access : accesses) {
    access.sequence = next_access_sequence_++;
    const auto channel = access.address.flat_channel;
    controllers_.at(channel)->admit_reserved(std::move(access), now());
    touched[channel] = true;
  }
  for (std::uint32_t channel = 0; channel < touched.size(); ++channel) {
    if (!touched[channel]) continue;
    schedule_refresh_due(channel);
    schedule_controller_wake(channel, now());
  }
}

void HbmSystem::schedule_controller_wake(std::uint32_t channel, SimTime when) {
  when = std::max(when, now());
  auto& controller = *controllers_.at(channel);
  if (!controller.mark_wakeup(when)) return;
  event_queue_.schedule(when, EventType::ControllerWakeup,
                        [this, channel, when] {
    controllers_.at(channel)->clear_wakeup(when);
    drive_controller(channel);
  });
}

void HbmSystem::schedule_refresh_due(std::uint32_t channel) {
  const auto due = controllers_.at(channel)->claim_refresh_event();
  if (!due.has_value()) return;
  event_queue_.schedule(*due, EventType::RefreshDue,
                        [this, channel, due = *due] {
    auto& controller = *controllers_.at(channel);
    controller.handle_refresh_event(due);
    schedule_controller_wake(channel, due);
    if (controller.has_outstanding()) schedule_refresh_due(channel);
  });
}

void HbmSystem::drive_controller(std::uint32_t channel) {
  const auto step = controllers_.at(channel)->drive(now());
  if (step.issued_access.has_value()) {
    const auto issued = *step.issued_access;
    event_queue_.schedule(
        issued.completion_time, EventType::TransactionCompletion,
        [this, access = issued.access, channel,
         completion_time = issued.completion_time] {
          finish_access(access, channel, completion_time);
        });
  }
  if (step.wake_at.has_value()) {
    schedule_controller_wake(channel, *step.wake_at);
  }
}

void HbmSystem::finish_access(const Access& access, std::uint32_t channel,
                              SimTime completion_time) {
  controllers_.at(channel)->complete_access(access, completion_time);
  auto parent = parents_.find(access.parent_id);
  if (parent == parents_.end()) throw std::logic_error("missing access parent");
  auto& request = parent->second;
  if (request.remaining_accesses == 0) {
    throw std::logic_error("completed access underflow");
  }
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
                           request.completion_time -
                               request.transaction.arrival_time};
  ++stats_.completed_transactions;
  if (completion.op == HbmOp::Read) stats_.read_bytes += completion.size_bytes;
  else stats_.write_bytes += completion.size_bytes;
  completions_.push_back(completion);
  if (completion_callback_) completion_callback_(completion);
  parents_.erase(iterator);
}

}  // namespace hbmsim
