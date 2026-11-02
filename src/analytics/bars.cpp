#include "itch/analytics/bars.hpp"

#include <algorithm>
#include <charconv>

namespace itch {

namespace {

constexpr std::int64_t kOpenNs = ((9LL * 3600) + (30LL * 60)) * 1'000'000'000LL;
constexpr std::int64_t kCloseNs = 16LL * 3600 * 1'000'000'000LL;

}  // namespace

std::uint64_t parse_interval_ns(std::string_view text) noexcept {
  if (text.size() < 2) {
    return 0;
  }
  const std::string_view digits = text.substr(0, text.size() - 1);
  std::uint64_t count = 0;
  const char* end = digits.data() + digits.size();
  const auto result = std::from_chars(digits.data(), end, count);
  if (result.ec != std::errc{} || result.ptr != end || count == 0) {
    return 0;
  }
  switch (text.back()) {
    case 's':
      return count * 1'000'000'000ULL;
    case 'm':
      return count * 60'000'000'000ULL;
    default:
      return 0;
  }
}

std::vector<BarRecord> build_bars(std::span<const TradeRecord> trades, const TradingDay& day,
                                  const BarOptions& options) {
  std::vector<BarRecord> bars;
  const auto interval = static_cast<std::int64_t>(options.interval_ns);
  std::int64_t bucket = -1;
  for (const TradeRecord& trade : trades) {
    const bool is_cross = trade.source == 'Q';
    if (!counts_toward_volume(trade) || trade.qty == 0 || (is_cross && !options.include_cross)) {
      continue;
    }
    const std::int64_t since_midnight = trade.ts - day.midnight_utc_ns();
    if (options.rth_only && (since_midnight < kOpenNs || since_midnight >= kCloseNs)) {
      continue;
    }
    const std::int64_t current = since_midnight / interval;
    if (current != bucket) {
      bucket = current;
      BarRecord bar;
      bar.ts_start = day.midnight_utc_ns() + (bucket * interval);
      bar.ts_end = bar.ts_start + interval;
      bar.open_ticks = trade.price_ticks;
      bar.high_ticks = trade.price_ticks;
      bar.low_ticks = trade.price_ticks;
      bar.locate = trade.locate;
      bars.push_back(bar);
    }
    BarRecord& bar = bars.back();
    bar.high_ticks = std::max(bar.high_ticks, trade.price_ticks);
    bar.low_ticks = std::min(bar.low_ticks, trade.price_ticks);
    bar.close_ticks = trade.price_ticks;
    bar.volume += trade.qty;
    bar.notional_tick_shares +=
        static_cast<UInt128>(trade.price_ticks) * static_cast<UInt128>(trade.qty);
    ++bar.trade_count;
    if (is_cross) {
      bar.cross_volume += trade.qty;
    }
  }
  return bars;
}

}  // namespace itch
