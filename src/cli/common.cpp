#include "common.hpp"

#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>

#include "commands.hpp"
#include "itch/io/mapped_file.hpp"

namespace itch::cli {

namespace {

std::int64_t now_ns() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

}  // namespace

void init_logging() {
  auto logger = spdlog::stderr_logger_st("itch");
  const char* level = std::getenv("ITCH_LOG_LEVEL");
  logger->set_level(level != nullptr ? spdlog::level::from_str(level) : spdlog::level::info);
  const char* format = std::getenv("ITCH_LOG_FORMAT");
  if (format != nullptr && std::string_view(format) == "json") {
    logger->set_pattern(R"({"ts":"%Y-%m-%dT%H:%M:%S.%e","level":"%l","msg":"%v"})");
  }
  spdlog::set_default_logger(std::move(logger));
}

std::optional<CivilDate> resolve_date(const std::string& date_option,
                                      const std::filesystem::path& file) {
  if (!date_option.empty()) {
    auto parsed = parse_date(date_option);
    if (!parsed) {
      throw CLI::ValidationError("--date", "expected YYYY-MM-DD, got " + date_option);
    }
    return parsed;
  }
  return date_from_file_name(file);
}

void emit(const std::string& path, const std::string& report) {
  if (path.empty()) {
    std::cout << report << '\n';
    return;
  }
  const std::filesystem::path target(path);
  if (target.has_parent_path()) {
    std::filesystem::create_directories(target.parent_path());
  }
  std::ofstream(target) << report << '\n';
}

int finish_run(const DriveResult& result) {
  if (result.truncated) {
    spdlog::error("truncated frame at byte offset {}", result.offset);
    return kExitMalformed;
  }
  if (result.stopped) {
    spdlog::error("malformed frame at byte offset {}", result.offset);
    return kExitMalformed;
  }
  return kExitOk;
}

ProgressLog::ProgressLog() : start_ns_(now_ns()) {}

void ProgressLog::operator()(const DriveResult& result, std::uint64_t offset,
                             std::uint64_t timestamp_ns) {
  const double elapsed = static_cast<double>(now_ns() - start_ns_) * 1e-9;
  spdlog::info("msgs={} rate={:.1f}M/s offset={} et={}", result.messages,
               static_cast<double>(result.messages) * 1e-6 / elapsed, offset,
               format_time_of_day(timestamp_ns));
}

BookRun run_books(const BookRunOptions& options) {
  const MappedFile file(options.path);
  BookRun run;
  run.manager = std::make_unique<BookManager>(
      BookConfig{parse_order_map(options.order_map), options.order_capacity});
  ProgressLog progress;
  BookManager& manager = *run.manager;
  const auto start = std::chrono::steady_clock::now();
  run.result = drive(file.bytes(), manager, {.until_timestamp_ns = options.until_timestamp_ns},
                     [&](const DriveResult& state, std::uint64_t offset) {
                       progress(state, offset, manager.last_timestamp());
                     });
  const std::chrono::duration<double> wall = std::chrono::steady_clock::now() - start;
  spdlog::info("applied {} messages in {:.3f} s, peak live orders {}", run.result.messages,
               wall.count(), manager.orders().peak_live());
  return run;
}

void add_book_options(CLI::App& command, BookRunOptions& options) {
  command.add_option("--order-map", options.order_map, "open_addressing, unordered_dense or std");
  command.add_option("--order-capacity", options.order_capacity, "Expected live orders");
}

std::uint64_t parse_time_of_day(const std::string& text) {
  unsigned hours = 0;
  unsigned minutes = 0;
  unsigned seconds = 0;
  int consumed = 0;
  const int fields =
      std::sscanf(text.c_str(), "%2u:%2u:%2u%n", &hours, &minutes, &seconds, &consumed);
  std::uint64_t nanos = 0;
  bool valid = fields == 3 && hours < 24 && minutes < 60 && seconds < 60;
  const std::string_view rest = std::string_view(text).substr(static_cast<std::size_t>(consumed));
  if (valid && !rest.empty()) {
    valid = rest[0] == '.' && rest.size() >= 2 && rest.size() <= 10;
    std::uint64_t scale = 100'000'000;
    for (std::size_t i = 1; valid && i < rest.size(); ++i) {
      valid = rest[i] >= '0' && rest[i] <= '9';
      nanos += static_cast<std::uint64_t>(rest[i] - '0') * scale;
      scale /= 10;
    }
  }
  if (!valid) {
    throw CLI::ValidationError("--time", "expected HH:MM:SS[.fraction], got " + text);
  }
  return ((hours * 3600ULL) + (minutes * 60ULL) + seconds) * 1'000'000'000ULL + nanos;
}

std::string format_time_of_day(std::uint64_t ns) {
  constexpr std::uint64_t kSecond = 1'000'000'000;
  const std::uint64_t seconds = ns / kSecond;
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%02llu:%02llu:%02llu.%09llu",
                static_cast<unsigned long long>(seconds / 3600),
                static_cast<unsigned long long>((seconds / 60) % 60),
                static_cast<unsigned long long>(seconds % 60),
                static_cast<unsigned long long>(ns % kSecond));
  return buffer;
}

}  // namespace itch::cli
