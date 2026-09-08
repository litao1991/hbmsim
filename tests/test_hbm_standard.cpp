#include "hbmsim/dram/hbm_standard.h"

#include <cassert>

int main() {
  const hbmsim::Hbm2Standard hbm2;
  assert(hbm2.profile_id() == "hbm2_2000");
  assert(hbm2.timing().t_rcd_rd == 14'000);
  assert(hbm2.timing().t_rcd_wr == 12'000);
  assert(hbm2.timing().t_ccd_s == 2'000);
  assert(hbm2.timing().t_wtr_l == 8'000);
  assert(hbm2.command_duration(hbmsim::HbmCommand::Act) == 2'000);
  assert(!hbm2.supports(hbmsim::HbmCommand::RfmPerBank));

  const hbmsim::Hbm3Standard hbm3;
  assert(hbm3.profile_id() == "hbm3_6400");
  assert(hbm3.timing().t_rcd_rd == 19'375);
  assert(hbm3.timing().t_rcd_wr == 9'375);
  assert(hbm3.timing().t_rrefd == 8'125);
  assert(hbm3.command_duration(hbmsim::HbmCommand::Act) == 938);
  assert(hbm3.command_duration(hbmsim::HbmCommand::PreBank) == 313);
  assert(hbm3.supports(hbmsim::HbmCommand::RfmPerBank));

  hbmsim::HbmBankState bank;
  hbm3.apply_transition(hbmsim::HbmCommand::Act, 42, bank);
  assert(bank.open_row && *bank.open_row == 42);
  hbm3.apply_transition(hbmsim::HbmCommand::ReadAuto, 42, bank);
  assert(!bank.open_row);
}
