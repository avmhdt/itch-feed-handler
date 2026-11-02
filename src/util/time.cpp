#include "itch/util/time.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>

namespace itch {

namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::Sunday;
using std::chrono::sys_days;
using std::chrono::weekday_indexed;
using std::chrono::year;
using std::chrono::year_month_day;

template <class T>
bool parse_field(std::string_view text, T& out) {
  const char* end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, out);
  return result.ec == std::errc{} && result.ptr == end;
}

std::optional<CivilDate> validated(int y, unsigned m, unsigned d) {
  const year_month_day ymd{year{y}, month{m}, day{d}};
  if (!ymd.ok()) {
    return std::nullopt;
  }
  return CivilDate{y, m, d};
}

sys_days to_days(const CivilDate& date) {
  return sys_days{year{date.year} / month{date.month} / day{date.day}};
}

}  // namespace

std::optional<CivilDate> parse_date(std::string_view text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
    return std::nullopt;
  }
  int y = 0;
  unsigned m = 0;
  unsigned d = 0;
  if (!parse_field(text.substr(0, 4), y) || !parse_field(text.substr(5, 2), m) ||
      !parse_field(text.substr(8, 2), d)) {
    return std::nullopt;
  }
  return validated(y, m, d);
}

std::optional<CivilDate> date_from_file_name(const std::filesystem::path& path) {
  const std::string name = path.filename().string();
  if (name.size() < 8) {
    return std::nullopt;
  }
  const std::string_view digits(name.data(), 8);
  int y = 0;
  unsigned m = 0;
  unsigned d = 0;
  if (!parse_field(digits.substr(0, 2), m) || !parse_field(digits.substr(2, 2), d) ||
      !parse_field(digits.substr(4, 4), y)) {
    return std::nullopt;
  }
  return validated(y, m, d);
}

std::string format_date(const CivilDate& date) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", date.year, date.month, date.day);
  return buffer;
}

int eastern_utc_offset_hours(const CivilDate& date) {
  const year y{date.year};
  const sys_days dst_start{y / std::chrono::March / weekday_indexed{Sunday, 2}};
  const sys_days dst_end{y / std::chrono::November / weekday_indexed{Sunday, 1}};
  const sys_days today = to_days(date);
  return today >= dst_start && today < dst_end ? -4 : -5;
}

TradingDay::TradingDay(const CivilDate& date) : date_(date) {
  using std::chrono::hours;
  using std::chrono::nanoseconds;
  const auto midnight = to_days(date).time_since_epoch() - hours{eastern_utc_offset_hours(date)};
  midnight_utc_ns_ = std::chrono::duration_cast<nanoseconds>(midnight).count();
}

}  // namespace itch
