#pragma once

#include "hbmsim/common/types.h"

#include <cstdint>
#include <vector>

namespace hbmsim {

struct ResourceStats {
  SimTime busy_time = 0;
  std::uint64_t issued_commands = 0;
};

struct QueueStats {
  std::uint64_t max_read_depth = 0;
  std::uint64_t max_write_depth = 0;
  std::uint64_t rejected_transactions = 0;
  std::uint64_t starvation_events = 0;
  SimTime queue_wait_time = 0;
  SimTime command_wait_time = 0;
  SimTime array_wait_time = 0;
  SimTime data_bus_wait_time = 0;
  SimTime refresh_stall_time = 0;
  SimTime data_service_time = 0;
  std::uint64_t merged_accesses = 0;
};

struct ChannelStats {
  std::uint64_t completed_bytes = 0;
  SimTime data_bus_busy_time = 0;
  std::uint64_t refreshes = 0;
  std::uint64_t per_bank_refreshes = 0;
  std::uint64_t rfm_events = 0;
  ResourceStats command_bus;
  ResourceStats row_command_bus;
  ResourceStats column_command_bus;
  QueueStats queue;
  std::vector<ResourceStats> pseudo_channels;
  std::vector<ResourceStats> banks;
};

// Mutually exclusive per-access latency stages. Their sum is exactly the
// access completion time minus enqueue time. Refresh interference is already
// charged to whichever stage was blocked; refresh_stall_time remains a
// separate controller-level causal diagnostic and is not added here.
struct HbmLatencyBreakdown {
  SimTime queue_wait = 0;
  SimTime command_phase = 0;
  SimTime data_ready = 0;
  SimTime data_bus_wait = 0;
  SimTime data_service = 0;

  [[nodiscard]] SimTime total() const {
    return queue_wait + command_phase + data_ready + data_bus_wait +
           data_service;
  }
};

struct HbmStats {
  std::uint64_t submitted_transactions = 0;
  std::uint64_t rejected_transactions = 0;
  std::uint64_t completed_transactions = 0;
  std::uint64_t modeled_accesses = 0;
  std::uint64_t issued_commands = 0;
  std::uint64_t read_bytes = 0;
  std::uint64_t write_bytes = 0;
  std::uint64_t row_hits = 0;
  std::uint64_t row_closed = 0;
  std::uint64_t row_conflicts = 0;
  std::uint64_t act_commands = 0;
  std::uint64_t pre_commands = 0;
  std::uint64_t read_commands = 0;
  std::uint64_t write_commands = 0;
  std::uint64_t rfm_events = 0;
  std::vector<ChannelStats> channels;
};

}  // namespace hbmsim
