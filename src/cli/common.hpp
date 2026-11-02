#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <CLI/CLI.hpp>

#include "itch/book/book_manager.hpp"
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

/// A book build over a whole file, or up to a time.
struct BookRun {
  std::unique_ptr<BookManager> manager;
  DriveResult result;
};

struct BookRunOptions {
  std::string path;
  std::string order_map = "open_addressing";
  std::size_t order_capacity = std::size_t{1} << 22;
  std::uint64_t until_timestamp_ns = kNoTimeLimit;
};

/// Applies a file to a fresh BookManager, logging progress (NFR-10).
[[nodiscard]] BookRun run_books(const BookRunOptions& options);

/// Adds the --order-map and --order-capacity options to a command.
void add_book_options(CLI::App& command, BookRunOptions& options);

/// Parses HH:MM:SS[.fraction] into nanoseconds since midnight.
[[nodiscard]] std::uint64_t parse_time_of_day(const std::string& text);

/// Formats nanoseconds since midnight as HH:MM:SS.nnnnnnnnn.
[[nodiscard]] std::string format_time_of_day(std::uint64_t ns);

}  // namespace itch::cli
