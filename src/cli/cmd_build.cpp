#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "commands.hpp"
#include "common.hpp"
#include "itch/io/mapped_file.hpp"
#include "itch/out/parquet_sink.hpp"
#include "itch/util/config.hpp"

namespace itch::cli {

namespace {

struct BuildOptions {
  std::string config = "configs/default.toml";
  std::string file;
  std::string date;
  std::string outputs_for;
  std::string datasets;
  std::string root;
  std::string report;
};

std::vector<std::string> split_commas(const std::string& text) {
  std::vector<std::string> out;
  std::stringstream stream(text);
  std::string item;
  while (std::getline(stream, item, ',')) {
    out.push_back(item);
  }
  return out;
}

SinkOptions sink_options(const Config& config, const CivilDate& date) {
  SinkOptions options;
  options.root = config.outputs_root;
  options.venue = config.venue;
  options.date = date;
  options.all_symbols = config.outputs_for == "all";
  if (!options.all_symbols) {
    options.symbols = read_watchlist(config.watchlist_file);
  }
  if (!config.datasets.empty()) {
    options.datasets = dataset_mask(config.datasets);
  }
  options.compression_level = config.compression_level;
  options.row_group_rows = config.row_group_rows;
  options.queue_capacity = config.queue_capacity;
  options.bar_intervals_ns.clear();
  for (const std::string& interval : config.bar_intervals) {
    const std::uint64_t ns = parse_interval_ns(interval);
    if (ns == 0) {
      throw CLI::ValidationError("bars.intervals", "cannot parse " + interval);
    }
    options.bar_intervals_ns.push_back(ns);
  }
  options.bars_rth_only = config.bar_session == "rth";
  options.bars_include_cross = config.bar_include_cross;
  options.snapshot_interval_ns = parse_interval_ns(config.snapshot_interval);
  options.snapshot_depth = config.snapshot_depth;
  return options;
}

/// Per-symbol volumes from the internal counters and from the corrected trades, for AC-W-03, and
/// the same day digest and invariant counters as `itch check`.
std::string report_json(const BookManager& manager, const Sinks& sinks, const CivilDate& date) {
  char digest[24];
  std::snprintf(digest, sizeof(digest), "%016llx",
                static_cast<unsigned long long>(manager.digest()));
  std::ostringstream json;
  json << "{\"broken_trades\":" << manager.counters().broken_trades
       << ",\"crossed_outside_auction\":" << manager.counters().crossed_outside_auction
       << ",\"date\":\"" << format_date(date) << "\",\"digest\":\"" << digest
       << "\",\"messages\":" << manager.messages() << ",\"symbols\":{";
  bool first = true;
  for (const std::uint16_t locate : sinks.wanted_locates()) {
    const SymbolVolume volume = sinks.volume(locate);
    const SymbolCounters& counters = manager.symbol_counters(locate);
    json << (first ? "" : ",") << '"' << manager.instruments()[locate].symbol()
         << "\":{\"broken_shares\":" << volume.broken_shares
         << ",\"closing_cross_shares\":" << volume.closing_cross_shares
         << ",\"closing_cross_ticks\":" << volume.closing_cross_ticks
         << ",\"counter_volume\":" << counters.volume - volume.broken_shares
         << ",\"cross_volume\":" << counters.cross_volume << ",\"trade_volume\":" << volume.volume
         << '}';
    first = false;
  }
  json << "}}";
  return json.str();
}

}  // namespace

void register_build(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("build", "Write every Parquet dataset for a day");
  auto options = std::make_shared<BuildOptions>();
  command->add_option("--config", options->config, "TOML config");
  command->add_option("--file", options->file, "Uncompressed ITCH file");
  command->add_option("--date", options->date, "Trading date, YYYY-MM-DD");
  command->add_option("--outputs-for", options->outputs_for, "watchlist or all")
      ->check(CLI::IsMember({"watchlist", "all"}));
  command->add_option("--datasets", options->datasets, "Comma-separated dataset names");
  command->add_option("--root", options->root, "Output root; overrides the config");
  command->add_option("--report", options->report, "Write a JSON summary here");

  command->callback([options, &exit_code] {
    Config config = load_config(options->config);
    if (!options->file.empty()) {
      config.input_path = options->file;
      config.input_date.clear();
    }
    if (!options->outputs_for.empty()) {
      config.outputs_for = options->outputs_for;
    }
    if (!options->datasets.empty()) {
      config.datasets = split_commas(options->datasets);
    }
    if (!options->root.empty()) {
      config.outputs_root = options->root;
    }
    const auto date =
        resolve_date(!options->date.empty() ? options->date : config.input_date, config.input_path);
    if (!date) {
      throw CLI::ValidationError("--date", "no trading date in the config, flag or file name");
    }

    const MappedFile file(config.input_path);
    Sinks sinks(sink_options(config, *date));
    auto manager = std::make_unique<BookManager>(
        BookConfig{parse_order_map(config.order_map), config.order_capacity_hint}, &sinks);

    ProgressLog progress;
    const auto start = std::chrono::steady_clock::now();
    const DriveResult result = drive(file.bytes(), *manager, {.strict = config.strict},
                                     [&](const DriveResult& state, std::uint64_t offset) {
                                       progress(state, offset, manager->last_timestamp());
                                     });
    sinks.finish(manager->broken_matches(), manager->instruments());
    const std::chrono::duration<double> wall = std::chrono::steady_clock::now() - start;
    spdlog::info("built {} messages in {:.3f} s", result.messages, wall.count());

    const std::string report = report_json(*manager, sinks, *date);
    if (!options->report.empty()) {
      emit(options->report, report);
    }
    exit_code = finish_run(result);
    if (exit_code == kExitOk && config.crossed_book == "fail" &&
        manager->counters().crossed_outside_auction != 0) {
      exit_code = kExitInvariant;
    }
  });
}

}  // namespace itch::cli
