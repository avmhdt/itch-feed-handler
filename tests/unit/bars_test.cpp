#include "itch/analytics/bars.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using itch::BarOptions;
using itch::TradeRecord;

constexpr std::int64_t kSecond = 1'000'000'000;
const itch::TradingDay kDay({2019, 1, 30});

TradeRecord trade(std::int64_t seconds_since_midnight, std::int64_t ticks, std::int64_t qty) {
  TradeRecord record;
  record.ts = kDay.to_utc_ns(static_cast<std::uint64_t>(seconds_since_midnight * kSecond));
  record.price_ticks = ticks;
  record.qty = qty;
  return record;
}

constexpr std::int64_t kTenOClock = 10 * 3600;

TEST(Intervals, Parse) {
  EXPECT_EQ(itch::parse_interval_ns("1s"), 1'000'000'000U);
  EXPECT_EQ(itch::parse_interval_ns("5m"), 300'000'000'000U);
  EXPECT_EQ(itch::parse_interval_ns("0s"), 0U);
  EXPECT_EQ(itch::parse_interval_ns("7h"), 0U);
  EXPECT_EQ(itch::parse_interval_ns("m"), 0U);
  EXPECT_EQ(itch::parse_interval_ns("xs"), 0U);
}

TEST(Bars, OhlcvAndVwapWithinOneInterval) {
  const std::vector<TradeRecord> trades{
      trade(kTenOClock + 1, 100'0000, 100), trade(kTenOClock + 2, 102'0000, 100),
      trade(kTenOClock + 3, 99'0000, 200), trade(kTenOClock + 4, 101'0000, 100)};
  const auto bars = itch::build_bars(trades, kDay, {});
  ASSERT_EQ(bars.size(), 1U);
  const itch::BarRecord& bar = bars[0];
  EXPECT_EQ(bar.open_ticks, 100'0000);
  EXPECT_EQ(bar.high_ticks, 102'0000);
  EXPECT_EQ(bar.low_ticks, 99'0000);
  EXPECT_EQ(bar.close_ticks, 101'0000);
  EXPECT_EQ(bar.volume, 500);
  EXPECT_EQ(bar.trade_count, 4);
  EXPECT_EQ(static_cast<std::uint64_t>(bar.notional_tick_shares),
            (100'0000ULL * 100) + (102'0000ULL * 100) + (99'0000ULL * 200) + (101'0000ULL * 100));
}

TEST(Bars, IntervalsAlignToTheEasternClockAndEmptyOnesAreSkipped) {
  const std::vector<TradeRecord> trades{trade(kTenOClock + 59, 100, 1),
                                        trade(kTenOClock + 60, 100, 1),
                                        trade(kTenOClock + 300, 100, 1)};
  const auto bars = itch::build_bars(trades, kDay, {});
  ASSERT_EQ(bars.size(), 3U);
  EXPECT_EQ(bars[0].ts_start, kDay.to_utc_ns(static_cast<std::uint64_t>(kTenOClock * kSecond)));
  EXPECT_EQ(bars[0].ts_end - bars[0].ts_start, 60 * kSecond);
  EXPECT_EQ(bars[1].ts_start, bars[0].ts_end);
  EXPECT_EQ(bars[2].ts_start - bars[0].ts_start, 300 * kSecond);
}

TEST(Bars, NonPrintableBrokenAndEmptyTradesAreLeftOut) {
  std::vector<TradeRecord> trades{trade(kTenOClock, 100, 10), trade(kTenOClock, 999, 10),
                                  trade(kTenOClock, 999, 10), trade(kTenOClock, 999, 0)};
  trades[1].printable = false;
  trades[2].broken = true;
  trades[3].source = 'Q';
  const auto bars = itch::build_bars(trades, kDay, {});
  ASSERT_EQ(bars.size(), 1U);
  EXPECT_EQ(bars[0].volume, 10);
  EXPECT_EQ(bars[0].high_ticks, 100);
}

TEST(Bars, CrossVolumeIsIncludedAndFlaggedUnlessTurnedOff) {
  std::vector<TradeRecord> trades{trade(kTenOClock, 100, 10), trade(kTenOClock + 1, 100, 90)};
  trades[1].source = 'Q';
  const auto with_cross = itch::build_bars(trades, kDay, {});
  EXPECT_EQ(with_cross[0].volume, 100);
  EXPECT_EQ(with_cross[0].cross_volume, 90);

  BarOptions options;
  options.include_cross = false;
  const auto without = itch::build_bars(trades, kDay, options);
  EXPECT_EQ(without[0].volume, 10);
  EXPECT_EQ(without[0].cross_volume, 0);
}

TEST(Bars, RegularHoursCanBeSelected) {
  const std::vector<TradeRecord> trades{trade((9 * 3600) + 1799, 100, 1),
                                        trade((9 * 3600) + 1800, 100, 2),
                                        trade((16 * 3600) - 1, 100, 4), trade(16 * 3600, 100, 8)};
  BarOptions options;
  options.rth_only = true;
  std::int64_t volume = 0;
  for (const itch::BarRecord& bar : itch::build_bars(trades, kDay, options)) {
    volume += bar.volume;
  }
  EXPECT_EQ(volume, 6);
}

}  // namespace
