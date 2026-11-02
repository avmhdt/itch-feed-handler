#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "itch/proto/messages.hpp"
#include "itch/state/instruments.hpp"

namespace itch {

/// Latest NOII for a symbol (ITCH 5.0 1.6), kept for book-at output.
struct NoiiSnapshot {
  std::uint64_t paired_shares = 0;
  std::uint64_t imbalance_shares = 0;
  std::uint64_t timestamp_ns = 0;
  std::uint32_t far_price = 0;
  std::uint32_t near_price = 0;
  std::uint32_t reference_price = 0;
  char direction = ' ';
  char cross_type = ' ';
  char price_variation = ' ';
  bool valid = false;
};

/// Per-symbol state from H, Y, h and the cross messages. A security missing from the pre-opening
/// spin must be treated as halted (ITCH 5.0 1.2.2), hence the default state.
struct SymbolState {
  NoiiSnapshot noii;
  std::array<char, 4> reason{' ', ' ', ' ', ' '};
  char trading_state = 'H';
  char reg_sho = '0';
  char operational_halt = 'T';
  bool opening_window = false;
  bool closing_window = false;
  bool halt_window = false;
  bool halt_cross_pending = false;  // A NOII announced a halt or IPO cross that has not printed.
  bool opening_done = false;
  bool closing_done = false;
};

/// Market-wide and per-symbol session state (FR-11, FR-12), including the cross windows of DQ-12.
class TradingState {
 public:
  TradingState() : symbols_(LocateTable::kSize) {}

  void on(const SystemEvent& message) noexcept { system_event_ = message.event_code(); }
  void on(const TradingAction& message) noexcept;
  void on(const RegSho& message) noexcept;
  void on(const OperationalHalt& message) noexcept;
  void on(const Noii& message) noexcept;
  void on(const CrossTrade& message) noexcept;
  void reset() noexcept;

  [[nodiscard]] const SymbolState& operator[](std::uint16_t locate) const noexcept {
    return symbols_[locate];
  }
  /// The latest System Event code, or a space before the first one.
  [[nodiscard]] char system_event() const noexcept { return system_event_; }
  /// True from start of system hours up to end of system hours, when books may trade.
  [[nodiscard]] bool system_open() const noexcept;
  /// DQ-12: whether a symbol is in a cross window at `timestamp_ns`. Windows open at the first
  /// NOII for the cross, or at the scheduled time for Nasdaq-listed symbols (09:28 for the open,
  /// 15:50 for the close), and close at the Q message. Halts are windows until the resume or the
  /// halt cross.
  [[nodiscard]] bool in_cross_window(std::uint16_t locate, const Instrument& instrument,
                                     std::uint64_t timestamp_ns) const noexcept;

 private:
  std::vector<SymbolState> symbols_;
  char system_event_ = ' ';
};

}  // namespace itch
