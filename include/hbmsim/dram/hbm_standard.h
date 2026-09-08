#pragma once

#include "hbmsim/dram/command.h"
#include "hbmsim/dram/spec.h"
#include "hbmsim/mapping/address_mapper.h"
#include "hbmsim/media/bank_state.h"
#include "hbmsim/resource/bandwidth_rate.h"
#include "hbmsim/timing/timing_spec.h"
#include "hbmsim/topology.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace hbmsim {

struct HbmOrganization {
  HbmTopology topology{};
  std::uint32_t columns_per_row = 0;
  std::uint32_t rows_per_bank = 0;
  std::uint64_t physical_burst_bytes = 32;
  std::uint64_t address_interleave_bytes = 32;
  AddressMapping address_mapping = AddressMapping::Linear;
  BandwidthRate pseudo_channel_rate{};
};

// Source-shaped cycle speed bin. Conversion to the simulator's picosecond
// timeline happens once, making upstream cycle-table diffs auditable.
struct HbmSpeedBin {
  SimTime tck_ps = 0;
  std::uint32_t n_bl = 0;
  std::uint32_t n_cl = 0;
  std::uint32_t n_rcd_rd = 0;
  std::uint32_t n_rcd_wr = 0;
  std::uint32_t n_rp = 0;
  std::uint32_t n_ras = 0;
  std::uint32_t n_wr = 0;
  std::uint32_t n_rtp = 0;
  std::uint32_t n_cwl = 0;
  std::uint32_t n_ccd_s = 0;
  std::uint32_t n_ccd_l = 0;
  std::uint32_t n_rrd_s = 0;
  std::uint32_t n_rrd_l = 0;
  std::uint32_t n_wtr_s = 0;
  std::uint32_t n_wtr_l = 0;
  std::uint32_t n_rtw = 0;
  std::uint32_t n_faw = 0;
  std::uint32_t n_ppd = 0;
  SimTime t_rfc_ps = 0;
  SimTime t_rfcpb_ps = 0;
  std::uint32_t n_ccd_r = 0;
  std::uint32_t n_rrefd = 0;
  SimTime t_refi_ps = 0;

  [[nodiscard]] HbmTimingSpec resolve() const;
};

enum class CommandCondition { TargetClosed, DifferentRow, TargetOpen, AnyOpen };
enum class CommandScope { Bank, Channel };
enum class CommandBus { Unified, Row, Column };
enum class BankTransition { None, OpenTarget, Close };

struct CommandPrerequisite {
  HbmCommand requested;
  CommandCondition condition;
  HbmCommand prerequisite;
};

struct CommandTransition {
  HbmCommand command;
  CommandScope scope;
  BankTransition transition;
};

struct CommandDecision {
  HbmCommand command = HbmCommand::Act;
  CommandScope scope = CommandScope::Bank;
  bool final_command = false;
};

class HbmStandard : public DramSpec {
 public:
  using DramSpec::supports;
  [[nodiscard]] virtual std::string_view profile_id() const = 0;
  [[nodiscard]] virtual const HbmOrganization& organization() const = 0;
  [[nodiscard]] virtual const HbmSpeedBin& speed_bin() const = 0;
  [[nodiscard]] virtual const HbmTimingSpec& timing() const = 0;
  [[nodiscard]] virtual std::span<const CommandPrerequisite> prerequisites() const = 0;
  [[nodiscard]] virtual std::span<const CommandTransition> transitions() const = 0;
  [[nodiscard]] virtual bool supports(HbmCommand command) const = 0;
  [[nodiscard]] virtual SimTime command_duration(HbmCommand command) const = 0;
  [[nodiscard]] virtual CommandBus command_bus(HbmCommand command) const;

  [[nodiscard]] CommandDecision resolve_prerequisite(
      HbmCommand requested, std::uint32_t target_row,
      const HbmBankState& target_bank, bool any_bank_open) const;
  [[nodiscard]] const CommandTransition& transition_for(
      HbmCommand command) const;
  void apply_transition(HbmCommand command, std::uint32_t target_row,
                        HbmBankState& bank) const;
};

class Hbm2Standard final : public HbmStandard {
 public:
  Hbm2Standard();
  [[nodiscard]] std::string_view name() const override { return "HBM2"; }
  [[nodiscard]] std::string_view profile_id() const override { return "hbm2_2000"; }
  [[nodiscard]] std::span<const AddressLevel> levels() const override;
  [[nodiscard]] bool supports(DramCommand command) const override;
  [[nodiscard]] bool supports(HbmCommand command) const override;
  [[nodiscard]] const HbmOrganization& organization() const override { return organization_; }
  [[nodiscard]] const HbmSpeedBin& speed_bin() const override { return speed_bin_; }
  [[nodiscard]] const HbmTimingSpec& timing() const override { return timing_; }
  [[nodiscard]] std::span<const CommandPrerequisite> prerequisites() const override;
  [[nodiscard]] std::span<const CommandTransition> transitions() const override;
  [[nodiscard]] SimTime command_duration(HbmCommand command) const override;

 private:
  HbmOrganization organization_;
  HbmSpeedBin speed_bin_;
  HbmTimingSpec timing_;
};

class Hbm3Standard final : public HbmStandard {
 public:
  Hbm3Standard();
  [[nodiscard]] std::string_view name() const override { return "HBM3"; }
  [[nodiscard]] std::string_view profile_id() const override { return "hbm3_6400"; }
  [[nodiscard]] std::span<const AddressLevel> levels() const override;
  [[nodiscard]] bool supports(DramCommand command) const override;
  [[nodiscard]] bool supports(HbmCommand command) const override;
  [[nodiscard]] const HbmOrganization& organization() const override { return organization_; }
  [[nodiscard]] const HbmSpeedBin& speed_bin() const override { return speed_bin_; }
  [[nodiscard]] const HbmTimingSpec& timing() const override { return timing_; }
  [[nodiscard]] std::span<const CommandPrerequisite> prerequisites() const override;
  [[nodiscard]] std::span<const CommandTransition> transitions() const override;
  [[nodiscard]] SimTime command_duration(HbmCommand command) const override;

 private:
  HbmOrganization organization_;
  HbmSpeedBin speed_bin_;
  HbmTimingSpec timing_;
};

class Hbm4Standard final : public HbmStandard {
 public:
  Hbm4Standard();
  [[nodiscard]] std::string_view name() const override { return "HBM4"; }
  [[nodiscard]] std::string_view profile_id() const override { return "hbm4_8000"; }
  [[nodiscard]] std::span<const AddressLevel> levels() const override;
  [[nodiscard]] bool supports(DramCommand command) const override;
  [[nodiscard]] bool supports(HbmCommand command) const override;
  [[nodiscard]] const HbmOrganization& organization() const override { return organization_; }
  [[nodiscard]] const HbmSpeedBin& speed_bin() const override { return speed_bin_; }
  [[nodiscard]] const HbmTimingSpec& timing() const override { return timing_; }
  [[nodiscard]] std::span<const CommandPrerequisite> prerequisites() const override;
  [[nodiscard]] std::span<const CommandTransition> transitions() const override;
  [[nodiscard]] SimTime command_duration(HbmCommand command) const override;

 private:
  HbmOrganization organization_;
  HbmSpeedBin speed_bin_;
  HbmTimingSpec timing_;
};

}  // namespace hbmsim
