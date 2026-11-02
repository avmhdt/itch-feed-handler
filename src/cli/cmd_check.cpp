#include <spdlog/spdlog.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "commands.hpp"
#include "common.hpp"

namespace itch::cli {

namespace {

struct CheckOptions {
  BookRunOptions run;
  std::string symbols = "all";
  std::string watchlist = "configs/watchlist.txt";
  std::string report;
  std::string date;
  std::string crossed_book = "count";
  bool per_symbol = false;
};

std::set<std::string> read_watchlist(const std::string& path) {
  std::ifstream file(path);
  if (!file) {
    throw CLI::ValidationError("--watchlist", "cannot read " + path);
  }
  std::set<std::string> symbols;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line[0] != '#') {
      symbols.insert(line);
    }
  }
  return symbols;
}

struct Totals {
  std::uint64_t accounting_failures = 0;
  std::uint64_t resting_orders = 0;
  std::int64_t resting_shares = 0;
};

Totals totals_of(const BookManager& manager) {
  Totals totals;
  totals.resting_orders = manager.orders().live();
  for (const Instrument* instrument : manager.instruments().all()) {
    const OrderBook& book = manager.book(instrument->locate);
    if (manager.accounting_gap(instrument->locate) != 0) {
      ++totals.accounting_failures;
    }
    const auto count = [&totals](const PriceLevel& level) { totals.resting_shares += level.qty; };
    book.for_each_level(Side::Bid, count);
    book.for_each_level(Side::Ask, count);
  }
  return totals;
}

void symbol_json(std::ostringstream& json, const BookManager& manager,
                 const Instrument& instrument) {
  const SymbolCounters& counters = manager.symbol_counters(instrument.locate);
  std::int64_t resting_shares = 0;
  std::int64_t resting_orders = 0;
  const auto count = [&](const PriceLevel& level) {
    resting_shares += level.qty;
    resting_orders += level.orders;
  };
  manager.book(instrument.locate).for_each_level(Side::Bid, count);
  manager.book(instrument.locate).for_each_level(Side::Ask, count);
  json << '"' << instrument.symbol() << "\":{\"added\":" << counters.added
       << ",\"cancelled\":" << counters.cancelled << ",\"cross_volume\":" << counters.cross_volume
       << ",\"crossed_outside\":" << counters.crossed_outside << ",\"deleted\":" << counters.deleted
       << ",\"executed\":" << counters.executed << ",\"locked_outside\":" << counters.locked_outside
       << ",\"replaced_out\":" << counters.replaced_out << ",\"resting_orders\":" << resting_orders
       << ",\"resting_shares\":" << resting_shares << ",\"volume_gross\":" << counters.volume
       << '}';
}

std::string report_json(const CheckOptions& options, const BookRun& run,
                        const std::optional<CivilDate>& date) {
  const BookManager& manager = *run.manager;
  const Counters& counters = manager.counters();
  const Totals totals = totals_of(manager);

  std::ostringstream json;
  json << "{\"accounting_failures\":" << totals.accounting_failures
       << ",\"broken_trades\":" << counters.broken_trades
       << ",\"crossed_in_auction\":" << counters.crossed_in_auction
       << ",\"crossed_outside_auction\":" << counters.crossed_outside_auction
       << ",\"crossed_in_settlement\":" << counters.crossed_in_settlement
       << ",\"crossed_samples\":[";
  for (std::size_t i = 0; i < counters.crossed_sample_count; ++i) {
    const CrossedSample& sample = counters.crossed_samples[i];
    json << (i == 0 ? "" : ",") << "{\"seq\":" << sample.seq << ",\"symbol\":\""
         << manager.instruments()[sample.locate].symbol() << "\"}";
  }
  json << "],\"date\":";
  if (date) {
    json << '"' << format_date(*date) << '"';
  } else {
    json << "null";
  }
  std::string_view longest_symbol;
  if (counters.longest_settlement_locate != 0) {
    longest_symbol = manager.instruments()[counters.longest_settlement_locate].symbol();
  }
  json << ",\"duplicate_adds\":" << counters.duplicate_adds
       << ",\"locked_in_auction\":" << counters.locked_in_auction
       << ",\"locked_in_settlement\":" << counters.locked_in_settlement
       << ",\"locked_outside_auction\":" << counters.locked_outside_auction
       << ",\"longest_settlement_ns\":" << counters.longest_settlement_ns
       << ",\"longest_settlement_symbol\":\"" << longest_symbol << '"'
       << ",\"messages\":" << run.result.messages << ",\"orphans\":" << counters.orphans
       << ",\"over_executions\":" << counters.over_executions
       << ",\"peak_live_orders\":" << manager.orders().peak_live()
       << ",\"resting_orders\":" << totals.resting_orders
       << ",\"resting_shares\":" << totals.resting_shares;
  if (options.per_symbol) {
    const std::set<std::string> wanted = options.symbols == "watchlist"
                                             ? read_watchlist(options.watchlist)
                                             : std::set<std::string>{};
    json << ",\"symbols\":{";
    bool first = true;
    for (const Instrument* instrument : manager.instruments().all()) {
      if (!wanted.empty() && !wanted.contains(std::string(instrument->symbol()))) {
        continue;
      }
      json << (first ? "" : ",");
      symbol_json(json, manager, *instrument);
      first = false;
    }
    json << '}';
  }
  json << ",\"timestamp_regressions\":" << counters.timestamp_regressions
       << ",\"undefined_locates\":" << counters.undefined_locates << '}';
  return json.str();
}

bool violated(const BookManager& manager) {
  const Counters& counters = manager.counters();
  return counters.crossed_outside_auction != 0 || counters.orphans != 0 ||
         counters.duplicate_adds != 0 || counters.over_executions != 0;
}

}  // namespace

void register_check(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("check", "Book invariants and order accounting");
  auto options = std::make_shared<CheckOptions>();
  command->add_option("--file", options->run.path, "Uncompressed ITCH file")->required();
  command->add_option("--symbols", options->symbols, "watchlist or all")
      ->check(CLI::IsMember({"watchlist", "all"}));
  command->add_option("--watchlist", options->watchlist, "Watchlist file");
  command->add_option("--report", options->report, "Write the JSON report here");
  command->add_option("--date", options->date, "Trading date, YYYY-MM-DD");
  command->add_option("--crossed-book", options->crossed_book, "count, fail or off")
      ->check(CLI::IsMember({"count", "fail", "off"}));
  command->add_flag("--per-symbol", options->per_symbol, "Include per-symbol totals in the report");
  add_book_options(*command, options->run);

  command->callback([options, &exit_code] {
    const auto date = resolve_date(options->date, options->run.path);
    const BookRun run = run_books(options->run);
    emit(options->report, report_json(*options, run, date));
    exit_code = finish_run(run.result);
    if (exit_code == kExitOk && options->crossed_book == "fail" && violated(*run.manager)) {
      spdlog::error("invariant violation; see the report");
      exit_code = kExitInvariant;
    }
  });
}

}  // namespace itch::cli
