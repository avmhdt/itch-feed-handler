#include "itch/state/trading_state.hpp"

#include <algorithm>

namespace itch {

namespace {

constexpr std::uint64_t ns_at(std::uint64_t hours, std::uint64_t minutes) {
  return (hours * 3600 + minutes * 60) * 1'000'000'000ULL;
}

constexpr std::uint64_t kOpeningWindowStart = ns_at(9, 28);
constexpr std::uint64_t kClosingWindowStart = ns_at(15, 50);

/// Market categories Q, G and S are the Nasdaq-listed tiers (ITCH 5.0 1.2.1). Nasdaq runs its
/// opening and closing crosses for these only.
constexpr bool nasdaq_listed(char market_category) {
  return market_category == 'Q' || market_category == 'G' || market_category == 'S';
}

}  // namespace

void TradingState::on(const TradingAction& message) noexcept {
  SymbolState& symbol = symbols_[message.stock_locate()];
  symbol.trading_state = message.trading_state();
  const std::string_view reason = message.reason();
  std::copy(reason.begin(), reason.end(), symbol.reason.begin());
  symbol.halt_window = message.trading_state() != 'T';
}

void TradingState::on(const RegSho& message) noexcept {
  symbols_[message.stock_locate()].reg_sho = message.action();
}

void TradingState::on(const OperationalHalt& message) noexcept {
  symbols_[message.stock_locate()].operational_halt = message.action();
}

void TradingState::on(const Noii& message) noexcept {
  SymbolState& symbol = symbols_[message.stock_locate()];
  symbol.noii = NoiiSnapshot{
      .paired_shares = message.paired_shares(),
      .imbalance_shares = message.imbalance_shares(),
      .timestamp_ns = message.timestamp_ns(),
      .far_price = message.far_price(),
      .near_price = message.near_price(),
      .reference_price = message.reference_price(),
      .direction = message.imbalance_direction(),
      .cross_type = message.cross_type(),
      .price_variation = message.price_variation(),
      .valid = true,
  };
  switch (message.cross_type()) {
    case 'O':
      symbol.opening_window = !symbol.opening_done;
      break;
    case 'C':
      symbol.closing_window = !symbol.closing_done;
      break;
    case 'H':
      symbol.halt_cross_pending = true;
      break;
    default:
      break;
  }
}

void TradingState::on(const CrossTrade& message) noexcept {
  SymbolState& symbol = symbols_[message.stock_locate()];
  switch (message.cross_type()) {
    case 'O':
      symbol.opening_window = false;
      symbol.opening_done = true;
      break;
    case 'C':
      symbol.closing_window = false;
      symbol.closing_done = true;
      break;
    default:
      symbol.halt_window = false;
      symbol.halt_cross_pending = false;
      break;
  }
}

void TradingState::reset() noexcept {
  std::fill(symbols_.begin(), symbols_.end(), SymbolState{});
  system_event_ = ' ';
}

bool TradingState::system_open() const noexcept {
  return system_event_ == 'S' || system_event_ == 'Q' || system_event_ == 'M';
}

bool TradingState::in_cross_window(std::uint16_t locate, const Instrument& instrument,
                                   std::uint64_t timestamp_ns) const noexcept {
  const SymbolState& symbol = symbols_[locate];
  if (symbol.opening_window || symbol.closing_window || symbol.halt_window ||
      symbol.halt_cross_pending) {
    return true;
  }
  if (!nasdaq_listed(instrument.market_category)) {
    return false;
  }
  return (timestamp_ns >= kOpeningWindowStart && !symbol.opening_done) ||
         (timestamp_ns >= kClosingWindowStart && !symbol.closing_done);
}

}  // namespace itch
