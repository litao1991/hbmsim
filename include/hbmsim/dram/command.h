#pragma once

namespace hbmsim {

enum class HbmCommand {
  Act,
  PreBank,
  PreAll,
  Read,
  Write,
  ReadAuto,
  WriteAuto,
  RefreshAllBank,
  RefreshPerBank,
  RfmAllBank,
  RfmPerBank,
  Count,
  Pre = PreBank,
};

}  // namespace hbmsim
