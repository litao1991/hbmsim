#include "hbmsim/dram/hbm_standard.h"

#include <stdexcept>

namespace hbmsim {
namespace {

constexpr std::array<AddressLevel, 7> kHbmLevels{
    AddressLevel::Channel, AddressLevel::PseudoChannel, AddressLevel::Sid,
    AddressLevel::BankGroup, AddressLevel::Bank, AddressLevel::Row,
    AddressLevel::Column};

constexpr std::array<CommandPrerequisite, 8> kPrerequisites{{
    {HbmCommand::Read, BankCondition::DifferentRow, HbmCommand::PreBank},
    {HbmCommand::Read, BankCondition::Closed, HbmCommand::Act},
    {HbmCommand::Write, BankCondition::DifferentRow, HbmCommand::PreBank},
    {HbmCommand::Write, BankCondition::Closed, HbmCommand::Act},
    {HbmCommand::ReadAuto, BankCondition::DifferentRow, HbmCommand::PreBank},
    {HbmCommand::ReadAuto, BankCondition::Closed, HbmCommand::Act},
    {HbmCommand::WriteAuto, BankCondition::DifferentRow, HbmCommand::PreBank},
    {HbmCommand::WriteAuto, BankCondition::Closed, HbmCommand::Act},
}};

constexpr std::array<CommandTransition, 11> kTransitions{{
    {HbmCommand::Act, BankTransition::OpenTarget},
    {HbmCommand::PreBank, BankTransition::Close},
    {HbmCommand::PreAll, BankTransition::Close},
    {HbmCommand::Read, BankTransition::None},
    {HbmCommand::Write, BankTransition::None},
    {HbmCommand::ReadAuto, BankTransition::Close},
    {HbmCommand::WriteAuto, BankTransition::Close},
    {HbmCommand::RefreshAllBank, BankTransition::Close},
    {HbmCommand::RefreshPerBank, BankTransition::Close},
    {HbmCommand::RfmAllBank, BankTransition::Close},
    {HbmCommand::RfmPerBank, BankTransition::Close},
}};

bool common_support(HbmCommand command) {
  return command == HbmCommand::Act || command == HbmCommand::PreBank ||
         command == HbmCommand::PreAll || command == HbmCommand::Read ||
         command == HbmCommand::Write || command == HbmCommand::ReadAuto ||
         command == HbmCommand::WriteAuto ||
         command == HbmCommand::RefreshAllBank ||
         command == HbmCommand::RefreshPerBank;
}

HbmCommand from_generic(DramCommand command) {
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

SimTime cycles(std::uint32_t count, SimTime tck) {
  return static_cast<SimTime>(count) * tck;
}

HbmOrganization hbm2_organization() {
  HbmOrganization value;
  value.topology = {1, 1, 2, 4, 4};
  value.columns_per_row = 32;
  value.rows_per_bank = 16'384;
  value.physical_burst_bytes = 32;
  value.address_interleave_bytes = 32;
  value.address_mapping = AddressMapping::Hbm2PseudoChannelBrc;
  value.pseudo_channel_rate = {32, 2'000};
  return value;
}

HbmOrganization hbm3_organization() {
  HbmOrganization value;
  value.topology = {1, 1, 2, 4, 4};
  value.columns_per_row = 32;
  value.rows_per_bank = 16'384;
  value.physical_burst_bytes = 32;
  value.address_interleave_bytes = 32;
  value.address_mapping = AddressMapping::Linear;
  value.pseudo_channel_rate = {32, 1'250};
  return value;
}

}  // namespace

HbmTimingSpec HbmSpeedBin::resolve() const {
  if (tck_ps == 0) throw std::invalid_argument("speed bin tCK must be non-zero");
  HbmTimingSpec t;
  t.use_extended_hbm_timing = true;
  t.t_cl = cycles(n_cl, tck_ps);
  t.t_rcd = cycles(n_rcd_rd, tck_ps);
  t.t_rcd_rd = cycles(n_rcd_rd, tck_ps);
  t.t_rcd_wr = cycles(n_rcd_wr, tck_ps);
  t.t_rp = cycles(n_rp, tck_ps);
  t.t_ras = cycles(n_ras, tck_ps);
  t.t_rc = t.t_ras + t.t_rp;
  t.t_wr = cycles(n_wr, tck_ps);
  t.t_rtp = cycles(n_rtp, tck_ps);
  t.t_cwl = cycles(n_cwl, tck_ps);
  t.t_bl = cycles(n_bl, tck_ps);
  t.t_ccd_s = cycles(n_ccd_s, tck_ps);
  t.t_ccd_l = cycles(n_ccd_l, tck_ps);
  t.t_ccd_r = cycles(n_ccd_r, tck_ps);
  t.t_ccd = t.t_ccd_s;
  t.t_rrd_s = cycles(n_rrd_s, tck_ps);
  t.t_rrd_l = cycles(n_rrd_l, tck_ps);
  t.t_rrd = t.t_rrd_s;
  t.t_faw = cycles(n_faw, tck_ps);
  t.t_wtr_s = cycles(n_wtr_s, tck_ps);
  t.t_wtr_l = cycles(n_wtr_l, tck_ps);
  t.t_wtr = t.t_cwl + t.t_bl + t.t_wtr_s;
  t.t_rtw = cycles(n_rtw, tck_ps);
  t.t_ppd = cycles(n_ppd, tck_ps);
  t.t_rrefd = cycles(n_rrefd, tck_ps);
  t.t_refi = t_refi_ps;
  t.t_rfc = t_rfc_ps;
  t.t_rfcpb = t_rfcpb_ps;
  t.t_rfmab = t_rfc_ps;
  t.t_rfmpb = t_rfcpb_ps;
  return t;
}

void HbmStandard::apply_transition(HbmCommand command,
                                   std::uint32_t target_row,
                                   HbmBankState& bank) const {
  for (const auto& rule : transitions()) {
    if (rule.command != command) continue;
    if (rule.transition == BankTransition::OpenTarget) {
      bank.open_row = target_row;
      bank.precharge_pending = false;
    } else if (rule.transition == BankTransition::Close) {
      bank.open_row.reset();
      bank.precharge_pending = false;
    }
    return;
  }
  throw std::logic_error("standard has no transition for command");
}

Hbm2Standard::Hbm2Standard()
    : organization_(hbm2_organization()),
      speed_bin_{1'000, 2, 14, 14, 12, 14, 34, 16, 5, 5,
                 2, 4, 4, 4, 6, 8, 15, 15, 0, 260'000, 160'000},
      timing_() {
  speed_bin_.n_ccd_r = 2;
  speed_bin_.n_rrefd = 8;
  speed_bin_.t_refi_ps = 3'900'000;
  timing_ = speed_bin_.resolve();
}

Hbm3Standard::Hbm3Standard()
    : organization_(hbm3_organization()),
      speed_bin_{625, 2, 20, 31, 15, 26, 45, 33, 9, 10,
                 2, 4, 4, 5, 7, 10, 17, 24, 2, 260'000, 200'000},
      timing_() {
  speed_bin_.n_ccd_r = 2;
  speed_bin_.n_rrefd = 13;
  speed_bin_.t_refi_ps = 3'900'000;
  timing_ = speed_bin_.resolve();
}

Hbm4Standard::Hbm4Standard() {
  organization_.topology = {1, 1, 2, 2, 8};
  organization_.columns_per_row = 256;
  organization_.rows_per_bank = 16'384;
  organization_.physical_burst_bytes = 32;
  organization_.address_interleave_bytes = 32;
  organization_.address_mapping = AddressMapping::Linear;
  organization_.pseudo_channel_rate = {64, 1'000};
  timing_ = HbmTimingSpec::hbm4_8000();
}

std::span<const AddressLevel> Hbm2Standard::levels() const { return kHbmLevels; }
std::span<const AddressLevel> Hbm3Standard::levels() const { return kHbmLevels; }
std::span<const AddressLevel> Hbm4Standard::levels() const { return kHbmLevels; }

bool Hbm2Standard::supports(DramCommand command) const {
  return supports(from_generic(command));
}
bool Hbm3Standard::supports(DramCommand command) const {
  return supports(from_generic(command));
}
bool Hbm4Standard::supports(DramCommand command) const {
  return supports(from_generic(command));
}

bool Hbm2Standard::supports(HbmCommand command) const {
  return common_support(command);
}
bool Hbm3Standard::supports(HbmCommand command) const {
  return common_support(command) || command == HbmCommand::RfmAllBank ||
         command == HbmCommand::RfmPerBank;
}
bool Hbm4Standard::supports(HbmCommand command) const {
  return common_support(command) || command == HbmCommand::RfmAllBank ||
         command == HbmCommand::RfmPerBank;
}

std::span<const CommandPrerequisite> Hbm2Standard::prerequisites() const {
  return kPrerequisites;
}
std::span<const CommandPrerequisite> Hbm3Standard::prerequisites() const {
  return kPrerequisites;
}
std::span<const CommandPrerequisite> Hbm4Standard::prerequisites() const {
  return kPrerequisites;
}
std::span<const CommandTransition> Hbm2Standard::transitions() const {
  return kTransitions;
}
std::span<const CommandTransition> Hbm3Standard::transitions() const {
  return kTransitions;
}
std::span<const CommandTransition> Hbm4Standard::transitions() const {
  return kTransitions;
}

SimTime Hbm2Standard::command_duration(HbmCommand command) const {
  return command == HbmCommand::Act ? 2 * speed_bin_.tck_ps : speed_bin_.tck_ps;
}

SimTime Hbm3Standard::command_duration(HbmCommand command) const {
  if (command == HbmCommand::Act) return (3 * speed_bin_.tck_ps + 1) / 2;
  if (command == HbmCommand::PreBank || command == HbmCommand::PreAll ||
      command == HbmCommand::RefreshAllBank ||
      command == HbmCommand::RefreshPerBank ||
      command == HbmCommand::RfmAllBank || command == HbmCommand::RfmPerBank) {
    return (speed_bin_.tck_ps + 1) / 2;
  }
  return speed_bin_.tck_ps;
}

SimTime Hbm4Standard::command_duration(HbmCommand) const {
  return timing_.t_command;
}

}  // namespace hbmsim
