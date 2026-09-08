#pragma once

#include "hbmsim/common/types.h"
#include "hbmsim/stats.h"

#include <cstdint>
#include <string>

namespace hbmsim {

enum class TrafficClass {
  Demand,
  Prefetch,
  Fill,
  Writeback,
  Background,
};

struct RequestMetadata {
  TrafficClass traffic_class = TrafficClass::Demand;
  std::uint8_t priority = 0;
  std::uint64_t opaque_tag = 0;
  std::uint32_t ordering_domain = 0;
};

struct HbmTransaction {
  TransactionId id = 0;
  HbmOp op = HbmOp::Read;
  std::uint64_t address = 0;
  std::uint64_t size_bytes = 0;
  SimTime arrival_time = 0;
  ClientId client = 0;
  RequestMetadata metadata{};
};

struct RequestToken {
  TransactionId id = 0;
};

enum class SubmitStatus {
  Accepted,
  Backpressure,
  InvalidArgument,
  DuplicateId,
  ArrivalInFuture
};

enum class HbmAccessClass { RowHit, RowClosed, RowConflict };

struct SubmitResult {
  SubmitStatus status = SubmitStatus::InvalidArgument;
  RequestToken token{};
  std::string message;

  [[nodiscard]] bool accepted() const { return status == SubmitStatus::Accepted; }
};

struct HbmCompletion {
  TransactionId id = 0;
  HbmOp op = HbmOp::Read;
  std::uint64_t address = 0;
  std::uint64_t size_bytes = 0;
  ClientId client = 0;
  std::uint32_t channel = 0;
  HbmAccessClass access_class = HbmAccessClass::RowClosed;
  SimTime arrival_time = 0;
  SimTime completion_time = 0;
  SimTime latency = 0;
  HbmLatencyBreakdown latency_breakdown{};
  RequestMetadata metadata{};
};

}  // namespace hbmsim
