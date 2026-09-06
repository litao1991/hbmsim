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
}
