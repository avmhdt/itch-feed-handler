#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace itch {

struct CivilDate {
  int year = 0;
  unsigned month = 0;
  unsigned day = 0;

  friend bool operator==(const CivilDate&, const CivilDate&) = default;
};

/// Parses YYYY-MM-DD; nullopt unless the text is a real calendar date.
[[nodiscard]] std::optional<CivilDate> parse_date(std::string_view text);

/// Takes the MMDDYYYY prefix of a sample file name, such as 01302019.NASDAQ_ITCH50.
[[nodiscard]] std::optional<CivilDate> date_from_file_name(const std::filesystem::path& path);

[[nodiscard]] std::string format_date(const CivilDate& date);

/// Eastern Time offset from UTC in hours for a date: -4 in daylight time, otherwise -5. Applies
/// the US rule in force since 2007, second Sunday of March to first Sunday of November. The day
/// of a change takes the offset that holds from 02:00, which is harmless because no market session
/// falls on a Sunday.
[[nodiscard]] int eastern_utc_offset_hours(const CivilDate& date);

/// Converts ITCH timestamps, nanoseconds since midnight Eastern (ITCH 5.0 "Data Types"), to
/// nanoseconds since the Unix epoch (DQ-08, FR-03). The offset is worked out once per day, so the
/// hot path is one addition and never touches a time zone database.
class TradingDay {
 public:
  explicit TradingDay(const CivilDate& date);

  [[nodiscard]] const CivilDate& date() const noexcept { return date_; }
  [[nodiscard]] std::int64_t midnight_utc_ns() const noexcept { return midnight_utc_ns_; }
  [[nodiscard]] std::int64_t to_utc_ns(std::uint64_t ns_since_midnight) const noexcept {
    return midnight_utc_ns_ + static_cast<std::int64_t>(ns_since_midnight);
  }

 private:
  CivilDate date_;
  std::int64_t midnight_utc_ns_;
};

}  // namespace itch
