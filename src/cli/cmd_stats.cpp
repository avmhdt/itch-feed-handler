#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>

#include "commands.hpp"
#include "itch/io/frame_reader.hpp"
#include "itch/io/mapped_file.hpp"

namespace itch::cli {

namespace {

/// M1 data smoke test: frames the whole file and counts the type byte of each frame.
std::string raw_type_counts_json(const std::string& path, int& exit_code) {
  const MappedFile file(path);
  FrameReader reader(file.bytes());
  std::array<std::uint64_t, 256> counts{};
  std::uint64_t total = 0;
  std::uint64_t empty = 0;

  const auto start = std::chrono::steady_clock::now();
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
  const std::chrono::duration<double> wall = std::chrono::steady_clock::now() - start;

  if (next == FrameReader::Next::Truncated) {
    std::cerr << "itch: truncated frame at byte offset " << reader.offset() << '\n';
    exit_code = kExitMalformed;
  }

  std::ostringstream json;
  json << "{\"bytes\":" << file.bytes().size() << ",\"by_type\":{";
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
  std::cerr << "itch: framed " << total << " messages in " << wall.count() << " s\n";
  return json.str();
}

}  // namespace

void register_stats(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("stats", "Message counts by type");
  auto path = std::make_shared<std::string>();
  auto raw_types = std::make_shared<bool>(false);
  auto json_out = std::make_shared<std::string>();
  command->add_option("--file", *path, "Uncompressed ITCH file")->required();
  command->add_flag("--raw-types", *raw_types, "Count type bytes only, without decoding");
  command->add_option("--json", *json_out, "Write the JSON report here");

  command->callback([path, raw_types, json_out, &exit_code] {
    if (!*raw_types) {
      throw CLI::ValidationError("stats", "only --raw-types is available in this build");
    }
    const std::string report = raw_type_counts_json(*path, exit_code);
    if (json_out->empty()) {
      std::cout << report << '\n';
    } else {
      std::ofstream(*json_out) << report << '\n';
    }
  });
}

}  // namespace itch::cli
