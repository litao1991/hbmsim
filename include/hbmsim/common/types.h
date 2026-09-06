#pragma once

#include <cstdint>

namespace hbmsim {

// The simulator's canonical clock is one picosecond.  This lets HBM timing
// and future HBF microsecond events share one integer timeline exactly.
using SimTime = std::uint64_t;
using TransactionId = std::uint64_t;
using ClientId = std::uint32_t;

inline constexpr SimTime kPicosecondsPerNanosecond = 1'000;

enum class HbmOp { Read, Write };

}  // namespace hbmsim
