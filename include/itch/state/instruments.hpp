#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "itch/proto/messages.hpp"

namespace itch {

/// Attributes of one security from its Stock Directory message (ITCH 5.0 1.2.1).
struct Instrument {
  std::array<char, 8> stock{};  // Space padded, as on the wire.
  std::uint32_t round_lot_size = 0;
  std::uint16_t locate = 0;
  char market_category = ' ';
  char financial_status = ' ';
  char issue_classification = ' ';
  std::array<char, 2> issue_subtype{' ', ' '};
  char authenticity = ' ';
  char luld_tier = ' ';
  char etp_flag = ' ';
  bool defined = false;

  [[nodiscard]] std::string_view symbol() const noexcept {
    return trim_right({stock.data(), stock.size()});
  }
};

/// Flat table indexed by stock locate (FR-04). ITCH 5.0 "Architecture": locates are low integers
/// meant to be array indices, so no symbol is ever hashed on the hot path.
class LocateTable {
 public:
  static constexpr std::size_t kSize = 65'536;

  LocateTable() : instruments_(kSize) {}

  void define(const StockDirectory& message) noexcept;
  void clear() noexcept;

  [[nodiscard]] const Instrument& operator[](std::uint16_t locate) const noexcept {
    return instruments_[locate];
  }
  [[nodiscard]] bool defined(std::uint16_t locate) const noexcept {
    return instruments_[locate].defined;
  }
  /// Slow symbol lookup for CLI use; returns 0, never a valid locate, when absent.
  [[nodiscard]] std::uint16_t find(std::string_view symbol) const noexcept;
  /// Defined instruments in locate order.
  [[nodiscard]] std::vector<const Instrument*> all() const;

 private:
  std::vector<Instrument> instruments_;
};

}  // namespace itch
