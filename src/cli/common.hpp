#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "itch/io/file_driver.hpp"
#include "itch/util/time.hpp"

namespace itch::cli {

/// Configures spdlog from ITCH_LOG_LEVEL and ITCH_LOG_FORMAT, logging to stderr (NFR-10).
void init_logging();

/// The trading date from --date when given, else from the file name.
[[nodiscard]] std::optional<CivilDate> resolve_date(const std::string& date_option,
                                                    const std::filesystem::path& file);

/// Writes a report to `path`, creating parent directories, or to stdout when `path` is empty.
void emit(const std::string& path, const std::string& report);

/// Logs a run's outcome and returns the exit code for it (DQ-01, DQ-02).
[[nodiscard]] int finish_run(const DriveResult& result);

/// Progress callback for drive(): messages, rate, offset and Eastern wall clock (NFR-10).
class ProgressLog {
 public:
  ProgressLog();
  void operator()(const DriveResult& result, std::uint64_t offset, std::uint64_t timestamp_ns);

 private:
  std::int64_t start_ns_;
};

/// Formats nanoseconds since midnight as HH:MM:SS.nnnnnnnnn.
[[nodiscard]] std::string format_time_of_day(std::uint64_t ns);

}  // namespace itch::cli
