#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "commands.hpp"
#include "common.hpp"

namespace itch::cli {

namespace {

struct BookAtOptions {
  BookRunOptions run;
  std::string symbol;
  std::string time;
  std::string date;
  std::string format = "table";
  std::size_t depth = 10;
  bool l3 = false;
};

std::string price_text(std::int64_t ticks) {
  char text[32];
  std::snprintf(text, sizeof(text), "%lld.%04lld", static_cast<long long>(ticks / 10000),
                static_cast<long long>(ticks % 10000));
  return text;
}

std::string noii_text(const NoiiSnapshot& noii) {
  if (!noii.valid) {
    return "none";
  }
  std::ostringstream text;
  text << "cross=" << noii.cross_type << " paired=" << noii.paired_shares
       << " imbalance=" << noii.imbalance_shares << " dir=" << noii.direction
       << " far=" << price_text(noii.far_price) << " near=" << price_text(noii.near_price)
       << " ref=" << price_text(noii.reference_price) << " var=" << noii.price_variation;
  return text.str();
}

std::string table_row(std::size_t row, const Level* bid, const Level* ask) {
  char left[64] = "";
  char right[64] = "";
  if (bid != nullptr) {
    std::snprintf(left, sizeof(left), "%10d%9lld%11s", bid->orders,
                  static_cast<long long>(bid->qty), price_text(bid->price_ticks).c_str());
  }
  if (ask != nullptr) {
    std::snprintf(right, sizeof(right), "%11s%9lld%9d", price_text(ask->price_ticks).c_str(),
                  static_cast<long long>(ask->qty), ask->orders);
  }
  char line[160];
  std::snprintf(line, sizeof(line), "%4zu%-30s |%-29s\n", row, left, right);
  std::string text(line);
  while (!text.empty() && text[text.size() - 2] == ' ') {
    text.erase(text.size() - 2, 1);
  }
  return text;
}

struct BookView {
  std::vector<Level> bids;
  std::vector<Level> asks;
};

BookView top_levels(const OrderBook& book, std::size_t depth) {
  BookView view{std::vector<Level>(depth), std::vector<Level>(depth)};
  view.bids.resize(book.levels(Side::Bid, view.bids));
  view.asks.resize(book.levels(Side::Ask, view.asks));
  return view;
}

std::string orders_table(const OrderBook& book, const BookView& view, Side side) {
  std::ostringstream text;
  const std::vector<Level>& levels = side == Side::Bid ? view.bids : view.asks;
  std::size_t index = 0;
  std::int64_t shown_price = -1;
  book.for_each_order(side, [&](const OrderNode& node) {
    if (static_cast<std::int64_t>(node.price) != shown_price) {
      shown_price = node.price;
      ++index;
    }
    if (index > levels.size()) {
      return;
    }
    text << (side == Side::Bid ? "BID " : "ASK ") << index - 1 << ' ' << price_text(node.price)
         << " ref=" << node.ref << " shares=" << node.shares << '\n';
  });
  return text.str();
}

std::string as_table(const BookAtOptions& options, const BookRun& run, const OrderBook& book,
                     std::uint16_t locate, const std::optional<CivilDate>& date) {
  const BookManager& manager = *run.manager;
  const SymbolState& state = manager.state()[locate];
  const BookView view = top_levels(book, options.depth);

  std::ostringstream text;
  text << options.symbol << "  " << (date ? format_date(*date) : "unknown-date") << ' '
       << format_time_of_day(parse_time_of_day(options.time)) << " ET  as of seq "
       << (manager.messages() == 0 ? 0 : manager.messages() - 1)
       << "  state=" << state.trading_state << "  regsho=" << state.reg_sho << '\n';
  text << "NOII: " << noii_text(state.noii) << '\n';
  text << " LVL   BID_ORD  BID_QTY     BID_PX |     ASK_PX  ASK_QTY  ASK_ORD\n";
  const std::size_t rows = std::max(view.bids.size(), view.asks.size());
  for (std::size_t row = 0; row < rows; ++row) {
    text << table_row(row, row < view.bids.size() ? &view.bids[row] : nullptr,
                      row < view.asks.size() ? &view.asks[row] : nullptr);
  }
  if (options.l3) {
    text << orders_table(book, view, Side::Bid) << orders_table(book, view, Side::Ask);
  }
  std::string out = text.str();
  out.pop_back();
  return out;
}

void levels_json(std::ostringstream& json, const std::vector<Level>& levels) {
  json << '[';
  bool first = true;
  for (const Level& level : levels) {
    json << (first ? "" : ",") << "{\"orders\":" << level.orders
         << ",\"price_ticks\":" << level.price_ticks << ",\"qty\":" << level.qty << '}';
    first = false;
  }
  json << ']';
}

void orders_json(std::ostringstream& json, const OrderBook& book, Side side) {
  json << '[';
  bool first = true;
  book.for_each_order(side, [&](const OrderNode& node) {
    json << (first ? "" : ",") << "{\"price_ticks\":" << node.price << ",\"ref\":" << node.ref
         << ",\"shares\":" << node.shares << '}';
    first = false;
  });
  json << ']';
}

std::string as_json(const BookAtOptions& options, const BookRun& run, const OrderBook& book,
                    std::uint16_t locate, const std::optional<CivilDate>& date) {
  const BookManager& manager = *run.manager;
  const SymbolState& state = manager.state()[locate];
  const BookView view = top_levels(book, options.depth);

  std::ostringstream json;
  json << "{\"asks\":";
  levels_json(json, view.asks);
  json << ",\"bids\":";
  levels_json(json, view.bids);
  json << ",\"date\":\"" << (date ? format_date(*date) : "") << "\",\"noii\":\""
       << noii_text(state.noii) << "\",";
  if (options.l3) {
    json << "\"orders_ask\":";
    orders_json(json, book, Side::Ask);
    json << ",\"orders_bid\":";
    orders_json(json, book, Side::Bid);
    json << ',';
  }
  json << "\"regsho\":\"" << state.reg_sho
       << "\",\"seq\":" << (manager.messages() == 0 ? 0 : manager.messages() - 1) << ",\"state\":\""
       << state.trading_state << "\",\"symbol\":\"" << options.symbol << "\",\"time\":\""
       << format_time_of_day(parse_time_of_day(options.time)) << "\"}";
  return json.str();
}

}  // namespace

void register_book_at(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("book-at", "Book of one symbol at a point in time");
  auto options = std::make_shared<BookAtOptions>();
  command->add_option("--file", options->run.path, "Uncompressed ITCH file")->required();
  command->add_option("--symbol", options->symbol, "Ticker")->required();
  command->add_option("--time", options->time, "Eastern time, HH:MM:SS[.fraction]")->required();
  command->add_option("--depth", options->depth, "Levels per side");
  command->add_flag("--l3", options->l3, "List the orders of each level in queue order");
  command->add_option("--format", options->format, "table or json")
      ->check(CLI::IsMember({"table", "json"}));
  command->add_option("--date", options->date, "Trading date, YYYY-MM-DD");
  add_book_options(*command, options->run);

  command->callback([options, &exit_code] {
    options->run.until_timestamp_ns = parse_time_of_day(options->time);
    const auto date = resolve_date(options->date, options->run.path);
    const BookRun run = run_books(options->run);
    const std::uint16_t locate = run.manager->instruments().find(options->symbol);
    if (locate == 0) {
      throw CLI::ValidationError("--symbol", "not in the stock directory: " + options->symbol);
    }
    const OrderBook& book = run.manager->book(locate);
    emit("", options->format == "json" ? as_json(*options, run, book, locate, date)
                                       : as_table(*options, run, book, locate, date));
    exit_code = finish_run(run.result);
  });
}

}  // namespace itch::cli
