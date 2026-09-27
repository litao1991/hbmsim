#include "hbmsim/hbm_system.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hbmsim {

DeviceSpec DeviceSpec::custom() {
  DeviceSpec device;
  device.organization_.topology = {};
  device.organization_.columns_per_row = 128;
  device.organization_.rows_per_bank = 16'384;
  device.organization_.physical_burst_bytes = 64;
  device.organization_.address_interleave_bytes = 64;
  device.organization_.address_mapping = AddressMapping::Linear;
  device.organization_.pseudo_channel_rate = {32, 1'000};
  return device;
}

DeviceSpec DeviceSpec::from_standard(
    std::shared_ptr<const HbmStandard> standard) {
  if (!standard) throw std::invalid_argument("device standard is null");
  DeviceSpec device;
  device.standard_ = std::move(standard);
  return device;
}

DeviceSpec DeviceSpec::as_custom() const {
  DeviceSpec device;
  device.organization_ = organization();
  device.timing_ = timing();
  return device;
}

HbmOrganization& DeviceSpec::edit_organization() {
  if (standard_) throw std::logic_error("standard organization is immutable; use as_custom");
  return organization_;
}

HbmTimingSpec& DeviceSpec::edit_timing() {
  if (standard_) throw std::logic_error("standard timing is immutable; use as_custom");
  return timing_;
}

void HbmConfig::validate() const {
  if (controller.max_active_transactions == 0)
    throw std::invalid_argument("max_active_transactions must be non-zero");
  const auto& organization = device.organization();
  organization.topology.validate();
  organization.pseudo_channel_rate.validate();
  if (organization.address_interleave_bytes == 0 ||
      organization.columns_per_row == 0 ||
      organization.rows_per_bank == 0 ||
      organization.physical_burst_bytes == 0) {
    throw std::invalid_argument("HBM geometry and burst sizes must be non-zero");
  }
  if (controller.write_drain_low_watermark >
      controller.write_drain_high_watermark) {
    throw std::invalid_argument("write drain low watermark exceeds high watermark");
  }
  if (simulation.simulation_access_granularity_bytes != 0 &&
      simulation.simulation_access_granularity_bytes <
          organization.physical_burst_bytes) {
    throw std::invalid_argument("simulation access granularity is below physical burst size");
  }
  if (simulation.simulation_access_granularity_bytes != 0 &&
      simulation.simulation_access_granularity_bytes %
              organization.physical_burst_bytes !=
          0) {
    throw std::invalid_argument("simulation access granularity must be a burst multiple");
  }
  const auto duration =
      refresh_duration(controller.refresh_policy, device.timing());
  if (controller.refresh_interval != 0 &&
      controller.refresh_interval < duration) {
    throw std::invalid_argument("refresh interval must be at least its duration");
  }
  if (controller.enable_rfm &&
      (!device.standard() ||
       !device.standard()->supports(HbmCommand::RfmPerBank))) {
    throw std::invalid_argument("selected standard does not support RFM");
  }
  if (controller.enable_rfm && controller.rfm_activation_threshold == 0) {
    throw std::invalid_argument("RFM requires a non-zero activation threshold");
  }
}

HbmConfig HbmConfig::hbm4_8000() {
  HbmConfig config;
  config.device =
      DeviceSpec::from_standard(std::make_shared<Hbm4Standard>());
  return config;
}

HbmConfig HbmConfig::hbm2_2000() {
  HbmConfig config;
  config.device =
      DeviceSpec::from_standard(std::make_shared<Hbm2Standard>());
  config.controller.request_merge_policy =
      RequestMergePolicy::SameAddressRead;
  return config;
}

HbmConfig HbmConfig::hbm3_6400() {
  HbmConfig config;
  config.device =
      DeviceSpec::from_standard(std::make_shared<Hbm3Standard>());
  config.controller.request_merge_policy =
      RequestMergePolicy::SameAddressRead;
  return config;
}

HbmSystem::HbmSystem(HbmConfig config, ISimScheduler& scheduler)
    : config_(std::move(config)), scheduler_(scheduler) {
  config_.validate();
  const auto& organization = config_.device.organization();
  const auto& policy = config_.controller;
  address_mapper_ = std::make_unique<HbmAddressMapper>(
      organization.topology, organization.address_interleave_bytes,
      organization.columns_per_row, organization.rows_per_bank,
      organization.address_mapping);
  const auto channel_count = organization.topology.channel_count();
  const auto banks_per_channel = address_mapper_->bank_count() / channel_count;
  stats_.channels.resize(channel_count);
  controllers_.reserve(channel_count);
  for (std::uint32_t channel = 0; channel < channel_count; ++channel) {
    auto& channel_stats = stats_.channels[channel];
    if (config_.simulation.detailed_stats) {
      channel_stats.pseudo_channels.resize(
          organization.topology.pseudo_channels_per_channel);
      channel_stats.banks.resize(banks_per_channel);
    }
    HbmControllerConfig controller_config;
    controller_config.channel = channel;
    controller_config.first_flat_bank = channel * banks_per_channel;
    controller_config.bank_count = banks_per_channel;
    controller_config.pseudo_channel_count =
        organization.topology.pseudo_channels_per_channel;
    controller_config.timing = config_.device.timing();
    controller_config.standard = config_.device.standard();
    controller_config.data_rate = organization.pseudo_channel_rate;
    controller_config.scheduler = policy.scheduler;
    controller_config.row_policy = policy.row_policy;
    controller_config.refresh_policy = policy.refresh_policy;
    controller_config.write_drain_high_watermark =
        policy.write_drain_high_watermark;
    controller_config.write_drain_low_watermark =
        policy.write_drain_low_watermark;
    controller_config.read_queue_capacity = policy.read_queue_capacity;
    controller_config.write_queue_capacity = policy.write_queue_capacity;
    controller_config.enable_request_merging =
        policy.request_merge_policy == RequestMergePolicy::SameAddressRead;
    controller_config.starvation_threshold = policy.starvation_threshold;
    controller_config.refresh_interval = policy.refresh_interval;
    controller_config.enable_rfm = policy.enable_rfm;
    controller_config.detailed_stats = config_.simulation.detailed_stats;
    controller_config.rfm_activation_threshold =
        policy.rfm_activation_threshold;
    controllers_.push_back(std::make_unique<HbmController>(
        controller_config, *address_mapper_, stats_));
  }
}

HbmSystem::~HbmSystem() {
  for (const auto token : pending_events_) scheduler_.cancel(token);
}

EventToken HbmSystem::schedule_event(SimTime when, EventCallback callback) {
  auto token = std::make_shared<EventToken>(0);
  *token = scheduler_.schedule_at(
      when, [this, token, callback = std::move(callback)]() mutable {
        pending_events_.erase(*token);
        callback();
      });
  pending_events_.insert(*token);
  return *token;
}

SubmitResult HbmSystem::try_submit_now(const HbmTransaction& transaction) {
  if (transaction.id == 0 || transaction.size_bytes == 0) {
    return {SubmitStatus::InvalidArgument, {},
            "transaction id and size_bytes must both be non-zero"};
  }
  if (transaction.arrival_time > now()) {
    return {SubmitStatus::ArrivalInFuture, {},
            "transaction must be submitted at or after its arrival time"};
  }
  if (parents_.contains(transaction.id)) {
    return {SubmitStatus::DuplicateId, {}, "transaction id was already submitted"};
  }
  if (transaction.size_bytes - 1 >
      std::numeric_limits<std::uint64_t>::max() - transaction.address)
    return {SubmitStatus::InvalidArgument, {}, "transaction address overflows"};
  const auto first = next_access(transaction, 0);
  const auto channel = first.address.flat_channel;
  const bool read = transaction.op == HbmOp::Read;
  if (parents_.size() >= config_.controller.max_active_transactions ||
      !controllers_[channel]->can_reserve(read, !read)) {
    ++stats_.rejected_transactions;
    ++stats_.channels[channel].queue.rejected_transactions;
    return {SubmitStatus::Backpressure, {}, "admission capacity exceeded"};
  }
  parents_.emplace(transaction.id, ParentRequest{transaction});
  pending_admissions_.push_back(transaction.id);
  ++stats_.submitted_transactions;
  pump_admissions();
  return {SubmitStatus::Accepted, RequestToken{transaction.id}, {}};
}

void HbmSystem::set_completion_callback(CompletionCallback callback) {
  completion_callback_ = std::move(callback);
}

void HbmSystem::set_capacity_callback(CapacityCallback callback) {
  capacity_callback_ = std::move(callback);
}

HbmSystem::Access HbmSystem::next_access(
    const HbmTransaction& transaction, std::uint64_t offset) const {
  auto address = transaction.address + offset;
  auto remaining = transaction.size_bytes - offset;
  const auto& organization = config_.device.organization();
  const auto max_group_bytes =
      config_.simulation.simulation_access_granularity_bytes == 0
          ? organization.physical_burst_bytes
          : config_.simulation.simulation_access_granularity_bytes;
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
    const auto burst_remainder = address % organization.physical_burst_bytes;
    const auto burst_boundary =
        organization.physical_burst_bytes - burst_remainder;
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
      if (group.has_value()) break;
      group = Access{LogicalAccess{transaction.id, transaction.op, mapped, bytes,
                                   transaction.arrival_time, transaction.client}};
      group->metadata = transaction.metadata;
      group->byte_address = address;
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
  return *group;
}

void HbmSystem::pump_admissions() {
  std::vector<bool> touched(controllers_.size(), false);
  for (auto it = pending_admissions_.begin(); it != pending_admissions_.end();) {
    auto& parent = parents_.at(*it);
    while (parent.admitted_bytes < parent.transaction.size_bytes) {
      auto access = next_access(parent.transaction, parent.admitted_bytes);
      const auto channel = access.address.flat_channel;
      auto& controller = *controllers_.at(channel);
      const bool read = access.op == HbmOp::Read;
      if (!controller.can_reserve(read, !read)) break;
      controller.reserve(read, !read);
      parent.admitted_bytes += access.size_bytes;
      ++parent.remaining_accesses;
      ++stats_.modeled_accesses;
      access.sequence = next_access_sequence_++;
      controller.admit_reserved(std::move(access), now());
      touched[channel] = true;
    }
    if (parent.admitted_bytes == parent.transaction.size_bytes)
      it = pending_admissions_.erase(it);
    else
      ++it;
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
  schedule_event(when, [this, channel, when] {
    controllers_.at(channel)->clear_wakeup(when);
    drive_controller(channel);
  });
}

void HbmSystem::schedule_refresh_due(std::uint32_t channel) {
  const auto due = controllers_.at(channel)->claim_refresh_event();
  if (!due.has_value()) return;
  schedule_event(*due, [this, channel, due = *due] {
    auto& controller = *controllers_.at(channel);
    controller.handle_refresh_event(due);
    schedule_controller_wake(channel, due);
    if (controller.has_outstanding()) schedule_refresh_due(channel);
  });
}

void HbmSystem::drive_controller(std::uint32_t channel) {
  const auto step = controllers_.at(channel)->drive(now());
  if (step.issued_access.has_value()) {
    pump_admissions();
    const auto issued = *step.issued_access;
    schedule_event(issued.completion_time, [this, issued, channel] {
      for (const auto& completion : issued.completions)
        finish_access(completion, channel, issued.completion_time);
    });
    if (capacity_callback_) {
      schedule_event(now(), [this, channel, when = now()] {
        const auto& controller = *controllers_.at(channel);
        const auto callback = capacity_callback_;
        if (callback) callback({when, channel, controller.available_read_slots(),
                                controller.available_write_slots()});
      });
    }
  }
  if (step.wake_at.has_value()) {
    schedule_controller_wake(channel, *step.wake_at);
  }
}

void HbmSystem::finish_access(const LogicalCompletion& access, std::uint32_t channel,
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
    request.last_latency_breakdown = access.latency_breakdown;
  }
  if (request.remaining_accesses == 0 &&
      request.admitted_bytes == request.transaction.size_bytes)
    complete_parent(access.parent_id);
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
                               request.transaction.arrival_time,
                           {}};
  // Parent latency follows its critical (last-completing) modeled access.
  // Every child is enqueued at the parent's arrival time, so that access's
  // exclusive stage partition also exactly partitions the parent latency.
  completion.latency_breakdown = request.last_latency_breakdown;
  completion.metadata = request.transaction.metadata;
  ++stats_.completed_transactions;
  if (completion.op == HbmOp::Read) stats_.read_bytes += completion.size_bytes;
  else stats_.write_bytes += completion.size_bytes;
  if (config_.simulation.retain_completions) {
    completions_.push_back(completion);
  }
  parents_.erase(iterator);
  const auto callback = completion_callback_;
  if (callback) callback(completion);
  // Completion also releases an active-parent credit, independently of slots.
  const auto capacity = capacity_callback_;
  if (capacity) {
    const auto& controller = *controllers_.at(completion.channel);
    capacity({now(), completion.channel, controller.available_read_slots(),
              controller.available_write_slots()});
  }
}

}  // namespace hbmsim
