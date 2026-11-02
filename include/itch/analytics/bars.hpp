#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "itch/out/schemas.hpp"
#include "itch/util/time.hpp"

namespace itch {

struct BarOptions {
  std::uint64_t interval_ns = 60'000'000'000ULL;
  bool rth_only = false;      ///< Keep only 09:30 to 16:00 Eastern.
  bool include_cross = true;  ///< D-09: cross volume counts, and is flagged in cross_volume.
};

/// Parses "1s", "5s", "1m" or "5m"-style intervals into nanoseconds; 0 for anything else.
[[nodiscard]] std::uint64_t parse_interval_ns(std::string_view text) noexcept;

/// Builds OHLCV/VWAP bars for one symbol from its corrected trades, in order (FR-14). Non-printable
/// and broken trades and zero-share crosses are left out, and intervals with no trade produce no
/// bar. Bars align to the Eastern wall clock.
[[nodiscard]] std::vector<BarRecord> build_bars(std::span<const TradeRecord> trades,
                                                const TradingDay& day, const BarOptions& options);

/// Whether a trade counts toward volume and bars (DQ-09).
[[nodiscard]] constexpr bool counts_toward_volume(const TradeRecord& trade) noexcept {
  return trade.printable && !trade.broken;
}

}  // namespace itch
