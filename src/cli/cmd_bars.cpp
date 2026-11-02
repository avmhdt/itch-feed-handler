#include <cstdint>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

#include "commands.hpp"
#include "common.hpp"
#include "itch/io/mapped_file.hpp"
#include "itch/out/parquet_sink.hpp"

namespace itch::cli {

namespace {

struct BarsOptions {
  std::string path;
  std::string symbol;
  std::string interval = "1m";
  std::string session = "all";
  std::string out;
  std::string date;
};

std::string bars_text(std::span<const BarRecord> bars, const TradingDay& day) {
  std::ostringstream text;
  text << "START_ET OPEN HIGH LOW CLOSE VOLUME VWAP TRADES CROSS_VOLUME\n";
  for (const BarRecord& bar : bars) {
    const double notional = static_cast<double>(bar.notional_tick_shares) / 10000.0;
    char line[160];
    std::snprintf(
        line, sizeof(line), "%.4f %.4f %.4f %.4f %lld %.4f %lld %lld\n",
        static_cast<double>(bar.open_ticks) / 10000.0,
        static_cast<double>(bar.high_ticks) / 10000.0, static_cast<double>(bar.low_ticks) / 10000.0,
        static_cast<double>(bar.close_ticks) / 10000.0, static_cast<long long>(bar.volume),
        notional / static_cast<double>(bar.volume), static_cast<long long>(bar.trade_count),
        static_cast<long long>(bar.cross_volume));
    text << format_time_of_day(static_cast<std::uint64_t>(bar.ts_start - day.midnight_utc_ns()))
         << ' ' << line;
  }
  std::string out = text.str();
  out.pop_back();
  return out;
}

}  // namespace

void register_bars(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("bars", "OHLCV and VWAP bars of one symbol");
  auto options = std::make_shared<BarsOptions>();
  command->add_option("--file", options->path, "Uncompressed ITCH file")->required();
  command->add_option("--symbol", options->symbol, "Ticker")->required();
  command->add_option("--interval", options->interval, "For example 1s, 5s, 1m, 5m");
  command->add_option("--session", options->session, "all or rth")
      ->check(CLI::IsMember({"all", "rth"}));
  command->add_option("--out", options->out, "Write Parquet here instead of printing");
  command->add_option("--date", options->date, "Trading date, YYYY-MM-DD");

  command->callback([options, &exit_code] {
    const std::uint64_t interval = parse_interval_ns(options->interval);
    if (interval == 0) {
      throw CLI::ValidationError("--interval", "cannot parse " + options->interval);
    }
    const auto date = resolve_date(options->date, options->path);
    if (!date) {
      throw CLI::ValidationError("--date", "no trading date in the flag or file name");
    }

    SinkOptions sink_options;
    sink_options.date = *date;
    sink_options.symbols = {options->symbol};
    sink_options.datasets = static_cast<unsigned>(Dataset::Trades);
    const MappedFile file(options->path);
    Sinks sinks(sink_options);
    auto manager = std::make_unique<BookManager>(BookConfig{}, &sinks);
    ProgressLog progress;
    const DriveResult result =
        drive(file.bytes(), *manager, {}, [&](const DriveResult& state, std::uint64_t offset) {
          progress(state, offset, manager->last_timestamp());
        });
    sinks.finish(manager->broken_matches(), manager->instruments());

    const std::uint16_t locate = manager->instruments().find(options->symbol);
    if (locate == 0) {
      throw CLI::ValidationError("--symbol", "not in the stock directory: " + options->symbol);
    }
    const auto bars = build_bars(sinks.trades(locate), sinks.day(),
                                 {.interval_ns = interval, .rth_only = options->session == "rth"});
    if (options->out.empty()) {
      emit("", bars_text(bars, sinks.day()));
    } else {
      write_bars_file(options->out, bars, sink_options.venue, options->symbol, options->interval);
    }
    exit_code = finish_run(result);
  });
}

}  // namespace itch::cli
