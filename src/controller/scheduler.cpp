#include "hbmsim/controller/scheduler.h"

#include <algorithm>

namespace hbmsim {
namespace {

class FrFcfsScheduler final : public IScheduler {
 public:
  [[nodiscard]] std::optional<SchedulerCandidate> choose(
      std::span<const SchedulerCandidate> candidates, SimTime now) const override {
    if (candidates.empty()) return std::nullopt;
    return *std::min_element(candidates.begin(), candidates.end(), [now](const auto& left, const auto& right) {
      const auto left_ready = left.ready_at <= now;
      const auto right_ready = right.ready_at <= now;
      if (left_ready != right_ready) return left_ready;
      if (!left_ready && left.ready_at != right.ready_at) return left.ready_at < right.ready_at;
      if (left.request_priority != right.request_priority) {
        return left.request_priority > right.request_priority;
      }
      const auto left_priority = left.data_command ? (left.row_hit ? 3 : 2) : 1;
      const auto right_priority = right.data_command ? (right.row_hit ? 3 : 2) : 1;
      if (left_priority != right_priority) return left_priority > right_priority;
      return left.sequence < right.sequence;
    });
  }
};

class FrFcfsRowHitScheduler final : public IScheduler {
 public:
  [[nodiscard]] std::optional<SchedulerCandidate> choose(
      std::span<const SchedulerCandidate> candidates, SimTime now) const override {
    if (candidates.empty()) return std::nullopt;
    return *std::min_element(candidates.begin(), candidates.end(), [now](const auto& left, const auto& right) {
      const auto left_ready = left.ready_at <= now;
      const auto right_ready = right.ready_at <= now;
      if (left_ready != right_ready) return left_ready;
      if (!left_ready && left.ready_at != right.ready_at) return left.ready_at < right.ready_at;
      if (left.request_priority != right.request_priority) {
        return left.request_priority > right.request_priority;
      }
      if (left.row_hit != right.row_hit) return left.row_hit;
      return left.sequence < right.sequence;
    });
  }
};

class FifoScheduler final : public IScheduler {
 public:
  [[nodiscard]] std::optional<SchedulerCandidate> choose(
      std::span<const SchedulerCandidate> candidates, SimTime) const override {
    if (candidates.empty()) return std::nullopt;
    return *std::min_element(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
      return left.sequence < right.sequence;
    });
  }
};

}  // namespace

const IScheduler& scheduler_for(SchedulerKind kind) {
  static const FrFcfsScheduler frfcfs;
  static const FrFcfsRowHitScheduler frfcfs_row_hit;
  static const FifoScheduler fifo;
  switch (kind) {
    case SchedulerKind::FrFcfs: return frfcfs;
    case SchedulerKind::FrFcfsRowHit: return frfcfs_row_hit;
    case SchedulerKind::Fifo: return fifo;
  }
  return frfcfs;
}

}  // namespace hbmsim
