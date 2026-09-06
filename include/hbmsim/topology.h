#pragma once

#include <cstdint>
#include <stdexcept>

namespace hbmsim {

// Pseudo-channels are explicit from H0.  H0 reserves the shared physical
// channel data bus; H1/H4 add pseudo-channel and bank-scoped resources.
struct HbmTopology {
  std::uint32_t stacks = 1;
  std::uint32_t channels_per_stack = 1;
  std::uint32_t pseudo_channels_per_channel = 2;
  std::uint32_t bank_groups_per_pseudo_channel = 1;
  std::uint32_t banks_per_bank_group = 1;

  void validate() const {
    if (stacks == 0 || channels_per_stack == 0 || pseudo_channels_per_channel == 0 ||
        bank_groups_per_pseudo_channel == 0 || banks_per_bank_group == 0) {
      throw std::invalid_argument("every HBM topology dimension must be non-zero");
    }
  }

  [[nodiscard]] std::uint32_t channel_count() const {
    return stacks * channels_per_stack;
  }
};

}  // namespace hbmsim
