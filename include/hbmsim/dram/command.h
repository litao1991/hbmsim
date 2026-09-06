#pragma once

namespace hbmsim {

enum class HbmCommand {
  Act, Pre, Read, Write, RefreshAllBank, RefreshPerBank, RfmAllBank, RfmPerBank,
};

}  // namespace hbmsim
