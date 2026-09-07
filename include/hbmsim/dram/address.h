#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace hbmsim {

// A standard owns the meaning and ordering of these levels.  HBM uses
// Stack/Channel/PseudoChannel/BankGroup/Bank/Row/Column; DDR omits Stack and
// PseudoChannel and adds Rank.
enum class AddressLevel { Stack, Channel, PseudoChannel, Rank, BankGroup, Bank, Row, Column };

struct AddressComponent {
  AddressLevel level;
  std::uint32_t index;
};

class Address {
 public:
  Address() = default;
  Address(std::initializer_list<AddressComponent> components) : components_(components) {}

  void set(AddressLevel level, std::uint32_t index) {
    for (auto& component : components_) {
      if (component.level == level) {
        component.index = index;
        return;
      }
    }
    components_.push_back({level, index});
  }

  [[nodiscard]] std::optional<std::uint32_t> find(AddressLevel level) const {
    for (const auto& component : components_) {
      if (component.level == level) return component.index;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::uint32_t at(AddressLevel level) const {
    if (const auto value = find(level); value.has_value()) return *value;
    throw std::out_of_range("address level is not present");
  }

  [[nodiscard]] const std::vector<AddressComponent>& components() const { return components_; }

 private:
  std::vector<AddressComponent> components_;
};

}  // namespace hbmsim
