#include "itch/util/time.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using itch::CivilDate;

constexpr std::uint64_t kNsPerHour = 3'600'000'000'000ULL;
constexpr std::uint64_t kMarketOpen = 9 * kNsPerHour + 1'800'000'000'000ULL;

TEST(Time, ParseDateAcceptsRealDatesOnly) {
  EXPECT_EQ(itch::parse_date("2019-01-30"), (CivilDate{2019, 1, 30}));
  EXPECT_FALSE(itch::parse_date("2019-02-30"));
  EXPECT_FALSE(itch::parse_date("2019-13-01"));
  EXPECT_FALSE(itch::parse_date("2019-1-30"));
  EXPECT_FALSE(itch::parse_date("2019/01/30"));
  EXPECT_FALSE(itch::parse_date("abcd-01-30"));
  EXPECT_FALSE(itch::parse_date("2019-ab-30"));
  EXPECT_FALSE(itch::parse_date("2019-01-3x"));
}

TEST(Time, DateFromFileName) {
  EXPECT_EQ(itch::date_from_file_name("/data/01302019.NASDAQ_ITCH50"), (CivilDate{2019, 1, 30}));
  EXPECT_EQ(itch::date_from_file_name("03272019.NASDAQ_ITCH50.gz"), (CivilDate{2019, 3, 27}));
  EXPECT_FALSE(itch::date_from_file_name("mini_day.itchbin"));
  EXPECT_FALSE(itch::date_from_file_name("short"));
  EXPECT_FALSE(itch::date_from_file_name("13302019.NASDAQ_ITCH50"));
}

TEST(Time, FormatDatePadsFields) { EXPECT_EQ(itch::format_date({2019, 3, 7}), "2019-03-07"); }

TEST(Time, OffsetAcrossBoth2019Transitions) {
  EXPECT_EQ(itch::eastern_utc_offset_hours({2019, 3, 9}), -5);
  EXPECT_EQ(itch::eastern_utc_offset_hours({2019, 3, 10}), -4);
  EXPECT_EQ(itch::eastern_utc_offset_hours({2019, 3, 11}), -4);
  EXPECT_EQ(itch::eastern_utc_offset_hours({2019, 11, 2}), -4);
  EXPECT_EQ(itch::eastern_utc_offset_hours({2019, 11, 3}), -5);
  EXPECT_EQ(itch::eastern_utc_offset_hours({2019, 11, 4}), -5);
}

TEST(Time, MarketOpenConvertsToUtcOnEachSampleDay) {
  // DQ-08: start of market hours is 14:30 UTC in winter and 13:30 UTC in daylight time.
  constexpr std::int64_t kSeconds = 1'000'000'000;
  const itch::TradingDay winter({2019, 1, 30});
  EXPECT_EQ(winter.to_utc_ns(kMarketOpen), 1'548'858'600 * kSeconds);  // 2019-01-30T14:30:00Z
  const itch::TradingDay summer({2019, 3, 27});
  EXPECT_EQ(summer.to_utc_ns(kMarketOpen), 1'553'693'400 * kSeconds);  // 2019-03-27T13:30:00Z
  const itch::TradingDay leap({2020, 1, 30});
  EXPECT_EQ(leap.to_utc_ns(kMarketOpen), 1'580'394'600 * kSeconds);  // 2020-01-30T14:30:00Z
  EXPECT_EQ(leap.date(), (CivilDate{2020, 1, 30}));
}

TEST(Time, MidnightIsTheConversionOrigin) {
  const itch::TradingDay day({2019, 1, 30});
  EXPECT_EQ(day.to_utc_ns(0), day.midnight_utc_ns());
}

}  // namespace
