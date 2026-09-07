#include "hbmsim/timing/timing_engine.h"

#include <cassert>

int main() {
  hbmsim::HbmAddress address;
  address.flat_bank = 3;
  address.flat_bank_group = 2;
  address.flat_pseudo_channel = 1;
  address.flat_channel = 0;

  {
    hbmsim::HbmTimingSpec spec;
    spec.t_rcd = 10;
    spec.t_rp = 7;
    spec.t_ras = 13;
    spec.t_rc = 20;
    spec.t_ccd = 5;
    spec.t_rrd = spec.t_faw = spec.t_wtr = spec.t_rtw = spec.t_rfc = 0;
    hbmsim::HbmTimingEngine engine(spec);
    engine.record(hbmsim::HbmCommand::Act, address, 100);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Read, address, 100) == 110);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Pre, address, 100) == 113);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Act, address, 100) == 120);
    engine.record(hbmsim::HbmCommand::Pre, address, 130);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Act, address, 130) == 137);
    engine.record(hbmsim::HbmCommand::Read, address, 200);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Read, address, 200) == 205);
  }

  {
    hbmsim::HbmTimingSpec spec;
    spec.t_rcd = spec.t_rp = spec.t_ras = spec.t_rc = spec.t_ccd = spec.t_rrd = 0;
    spec.t_faw = spec.t_rtw = spec.t_rfc = 0;
    spec.t_wtr = 9;
    hbmsim::HbmTimingEngine engine(spec);
    engine.record(hbmsim::HbmCommand::Write, address, 100);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Read, address, 100) == 109);
  }

  {
    hbmsim::HbmTimingSpec spec;
    spec.t_rcd = spec.t_rp = spec.t_ras = spec.t_rc = spec.t_ccd = spec.t_rrd = 0;
    spec.t_faw = 20;
    spec.t_wtr = spec.t_rtw = spec.t_rfc = 0;
    hbmsim::HbmTimingEngine engine(spec);
    engine.record(hbmsim::HbmCommand::Act, address, 0);
    engine.record(hbmsim::HbmCommand::Act, address, 1);
    engine.record(hbmsim::HbmCommand::Act, address, 2);
    engine.record(hbmsim::HbmCommand::Act, address, 3);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Act, address, 4) == 20);
  }

  // V0.5: canonical profiles distinguish RD/WR activation delays, short and
  // long column/activation spacing, and CAS-to-PRE constraints.
  {
    hbmsim::HbmTimingSpec spec;
    spec.use_extended_hbm_timing = true;
    spec.t_rcd_rd = 11;
    spec.t_rcd_wr = 7;
    spec.t_rp = spec.t_ras = spec.t_rc = spec.t_faw = 0;
    spec.t_ccd_s = 3;
    spec.t_ccd_l = 8;
    spec.t_rrd_s = 4;
    spec.t_rrd_l = 9;
    spec.t_wtr_s = 5;
    spec.t_wtr_l = 7;
    spec.t_rtw = 0;
    spec.t_rtp = 6;
    spec.t_wr = 11;
    spec.t_cwl = 2;
    spec.t_bl = 1;
    spec.t_rfc = spec.t_rfcpb = spec.t_rfmab = spec.t_rfmpb = 0;
    hbmsim::HbmTimingEngine engine(spec);
    auto other_group = address;
    other_group.flat_bank = 4;
    other_group.flat_bank_group = 3;
    engine.record(hbmsim::HbmCommand::Act, address, 100);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Read, address, 100) == 111);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Write, address, 100) == 107);
    engine.record(hbmsim::HbmCommand::Act, address, 200);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Act, other_group, 200) == 204);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Act, address, 200) == 209);
    engine.record(hbmsim::HbmCommand::Read, address, 300);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Read, other_group, 300) == 303);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Read, address, 300) == 308);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Pre, address, 300) == 306);
    engine.record(hbmsim::HbmCommand::Write, address, 400);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Read, other_group, 400) == 408);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Read, address, 400) == 410);
    assert(engine.earliest_issue(hbmsim::HbmCommand::Pre, address, 400) == 414);
  }
}
