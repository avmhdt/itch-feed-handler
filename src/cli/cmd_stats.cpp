#include <spdlog/spdlog.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "commands.hpp"
#include "common.hpp"
#include "itch/io/file_driver.hpp"
#include "itch/io/mapped_file.hpp"
#include "type_counter.hpp"

namespace itch::cli {

namespace {

/// M1 data smoke test: frames the file and counts the first byte of each frame, undecoded.
std::string raw_type_counts_json(std::span<const std::byte> bytes, int& exit_code) {
  FrameReader reader(bytes);
  std::array<std::uint64_t, 256> counts{};
  std::uint64_t total = 0;
  std::uint64_t empty = 0;

  std::span<const std::byte> frame;
  FrameReader::Next next = FrameReader::Next::End;
  while ((next = reader.next(frame)) == FrameReader::Next::Frame) {
    ++total;
    if (frame.empty()) {
      ++empty;
      continue;
    }
    ++counts[std::to_integer<std::size_t>(frame[0])];
  }

  if (next == FrameReader::Next::Truncated) {
    spdlog::error("truncated frame at byte offset {}", reader.offset());
    exit_code = kExitMalformed;
  }

  std::ostringstream json;
  json << "{\"bytes\":" << bytes.size() << ",\"by_type\":{";
  bool first = true;
  for (std::size_t type = 0; type < counts.size(); ++type) {
    if (counts[type] == 0) {
      continue;
    }
    char key[8];
    std::snprintf(key, sizeof(key), "%02x", static_cast<unsigned>(type));
    json << (first ? "" : ",") << "\"0x" << key << "\":" << counts[type];
    first = false;
  }
  json << "},\"empty_frames\":" << empty << ",\"total\":" << total
       << ",\"truncated\":" << (next == FrameReader::Next::Truncated ? "true" : "false") << '}';
  return json.str();
}

struct StatsOptions {
  std::string path;
  std::string date;
  std::string json_out;
  std::uint64_t limit = 0;
  bool raw_types = false;
  bool lenient = false;
};

}  // namespace

std::string counts_json(const TypeCounter& counter, std::uint64_t messages, std::uint64_t malformed,
                        std::size_t bytes, const std::optional<CivilDate>& date) {
  std::ostringstream json;
  json << "{\"by_type\":{";
  bool first = true;
  for (std::size_t type = 0; type < counter.counts().size(); ++type) {
    if (counter.counts()[type] == 0) {
      continue;
    }
    json << (first ? "" : ",") << '"' << static_cast<char>(type) << "\":" << counter.counts()[type];
    first = false;
  }
  json << "},\"bytes\":" << bytes << ",\"date\":";
  if (date) {
    json << '"' << format_date(*date) << '"';
  } else {
    json << "null";
  }
  json << ",\"first_timestamp_ns\":" << counter.first_timestamp()
       << ",\"last_timestamp_ns\":" << counter.last_timestamp() << ",\"malformed\":" << malformed
       << ",\"system_events\":[";
  first = true;
  for (const SessionEvent& event : counter.events()) {
    json << (first ? "" : ",") << "{\"code\":\"" << event.code << "\",\"et\":\""
         << format_time_of_day(event.timestamp_ns) << "\",\"utc_ns\":";
    if (date) {
      json << TradingDay(*date).to_utc_ns(event.timestamp_ns);
    } else {
      json << "null";
    }
    json << '}';
    first = false;
  }
  json << "],\"total\":" << messages << '}';
  return json.str();
}

void register_stats(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("stats", "Message counts by type");
  auto options = std::make_shared<StatsOptions>();
  command->add_option("--file", options->path, "Uncompressed ITCH file")->required();
  command->add_option("--limit", options->limit, "Stop after this many messages");
  command->add_option("--json", options->json_out, "Write the JSON report here");
  command->add_option("--date", options->date, "Trading date, YYYY-MM-DD");
  command->add_flag("--raw-types", options->raw_types, "Count type bytes without decoding");
  command->add_flag("--lenient", options->lenient, "Count and skip malformed frames");

  command->callback([options, &exit_code] {
    const MappedFile file(options->path);
    if (options->raw_types) {
      emit(options->json_out, raw_type_counts_json(file.bytes(), exit_code));
      return;
    }

    TypeCounter counter;
    ProgressLog progress;
    const auto start = std::chrono::steady_clock::now();
    const DriveResult result =
        drive(file.bytes(), counter, {.strict = !options->lenient, .limit = options->limit},
              [&](const DriveResult& state, std::uint64_t offset) {
                progress(state, offset, counter.last_timestamp());
              });
    const std::chrono::duration<double> wall = std::chrono::steady_clock::now() - start;

    spdlog::info("parsed {} messages in {:.3f} s", result.messages, wall.count());
    emit(options->json_out,
         counts_json(counter, result.messages, result.malformed, file.bytes().size(),
                     resolve_date(options->date, options->path)));
    exit_code = finish_run(result);
  });
}

}  // namespace itch::cli
