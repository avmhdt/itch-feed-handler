#include "itch/out/parquet_sink.hpp"

#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/writer.h>
#include <parquet/properties.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace itch {

namespace {

using arrow::field;
using SchemaPtr = std::shared_ptr<arrow::Schema>;
using ArrayPtr = std::shared_ptr<arrow::Array>;

constexpr double kTicksPerDollar = 10000.0;

template <class T>
T ok(arrow::Result<T> result) {
  if (!result.ok()) {
    throw std::runtime_error(result.status().ToString());
  }
  return result.MoveValueUnsafe();
}

void ok(const arrow::Status& status) {
  if (!status.ok()) {
    throw std::runtime_error(status.ToString());
  }
}

double dollars(std::int64_t ticks) { return static_cast<double>(ticks) / kTicksPerDollar; }

std::string mpid_text(std::uint32_t packed) {
  std::string text(4, ' ');
  for (std::size_t i = 0; i < 4; ++i) {
    text[i] = static_cast<char>((packed >> ((3 - i) * 8U)) & 0xFFU);
  }
  return text;
}

constexpr std::string_view event_name(char event) {
  switch (event) {
    case 'a':
      return "add";
    case 'e':
      return "exec";
    case 'c':
      return "cancel";
    case 'd':
      return "delete";
    default:
      return "replace";
  }
}

/// A column built from one value per row. `value` returns the cell, or nullopt for null.
template <class Builder, class Rows, class Value>
ArrayPtr column(const Rows& rows, Value value) {
  Builder builder;
  ok(builder.Reserve(static_cast<std::int64_t>(rows.size())));
  for (const auto& row : rows) {
    const auto cell = value(row);
    if constexpr (requires { cell.has_value(); }) {
      if (cell.has_value()) {
        ok(builder.Append(*cell));
      } else {
        ok(builder.AppendNull());
      }
    } else {
      ok(builder.Append(cell));
    }
  }
  return ok(builder.Finish());
}

template <class Rows>
ArrayPtr constant(const Rows& rows, const std::string& text) {
  return column<arrow::StringBuilder>(rows, [&text](const auto&) { return text; });
}

SchemaPtr book_l3_schema() {
  return arrow::schema({
      field("ts_exchange", arrow::int64(), false),
      field("ts_recv", arrow::int64(), false),
      field("venue", arrow::utf8(), false),
      field("symbol", arrow::utf8(), false),
      field("order_id", arrow::uint64(), false),
      field("event", arrow::utf8(), false),
      field("side", arrow::int8(), false),
      field("price_ticks", arrow::int64(), false),
      field("qty", arrow::int64(), false),
      field("new_order_id", arrow::uint64(), true),
      field("match_id", arrow::uint64(), true),
      field("seq", arrow::int64(), false),
      field("msg_type", arrow::utf8(), false),
      field("tracking_number", arrow::uint16(), false),
      field("mpid", arrow::utf8(), true),
      field("printable", arrow::boolean(), true),
  });
}

SchemaPtr trades_schema() {
  return arrow::schema({
      field("ts_exchange", arrow::int64(), false),
      field("ts_recv", arrow::int64(), false),
      field("venue", arrow::utf8(), false),
      field("symbol", arrow::utf8(), false),
      field("trade_id", arrow::utf8(), false),
      field("price", arrow::float64(), false),
      field("qty", arrow::float64(), false),
      field("side", arrow::int8(), false),
      field("price_ticks", arrow::int64(), false),
      field("source", arrow::utf8(), false),
      field("cross_type", arrow::utf8(), true),
      field("printable", arrow::boolean(), false),
      field("broken", arrow::boolean(), false),
      field("seq", arrow::int64(), false),
  });
}

SchemaPtr bbo_schema() {
  return arrow::schema({
      field("ts", arrow::int64(), false),
      field("venue", arrow::utf8(), false),
      field("symbol", arrow::utf8(), false),
      field("bid_px", arrow::float64(), true),
      field("ask_px", arrow::float64(), true),
      field("bid_qty", arrow::float64(), false),
      field("ask_qty", arrow::float64(), false),
      field("seq", arrow::int64(), false),
      field("bid_orders", arrow::int32(), false),
      field("ask_orders", arrow::int32(), false),
  });
}

SchemaPtr bars_schema() {
  return arrow::schema({
      field("ts_start", arrow::int64(), false),
      field("ts_end", arrow::int64(), false),
      field("venue", arrow::utf8(), false),
      field("symbol", arrow::utf8(), false),
      field("interval", arrow::utf8(), false),
      field("open", arrow::float64(), false),
      field("high", arrow::float64(), false),
      field("low", arrow::float64(), false),
      field("close", arrow::float64(), false),
      field("volume", arrow::int64(), false),
      field("notional", arrow::float64(), false),
      field("vwap", arrow::float64(), false),
      field("trade_count", arrow::int64(), false),
      field("cross_volume", arrow::int64(), false),
  });
}

SchemaPtr snapshots_schema() {
  return arrow::schema({
      field("ts", arrow::int64(), false),
      field("venue", arrow::utf8(), false),
      field("symbol", arrow::utf8(), false),
      field("side", arrow::int8(), false),
      field("level", arrow::uint16(), false),
      field("price_ticks", arrow::int64(), false),
      field("price", arrow::float64(), false),
      field("qty", arrow::int64(), false),
      field("order_count", arrow::int32(), false),
  });
}

SchemaPtr trading_state_schema() {
  return arrow::schema({
      field("ts_exchange", arrow::int64(), false),
      field("ts_recv", arrow::int64(), false),
      field("venue", arrow::utf8(), false),
      field("symbol", arrow::utf8(), true),
      field("msg_type", arrow::utf8(), false),
      field("state", arrow::utf8(), false),
      field("reason", arrow::utf8(), true),
      field("details", arrow::utf8(), false),
      field("seq", arrow::int64(), false),
  });
}

SchemaPtr noii_schema() {
  return arrow::schema({
      field("ts_exchange", arrow::int64(), false),
      field("ts_recv", arrow::int64(), false),
      field("venue", arrow::utf8(), false),
      field("symbol", arrow::utf8(), false),
      field("paired_shares", arrow::int64(), false),
      field("imbalance_shares", arrow::int64(), false),
      field("imbalance_direction", arrow::utf8(), false),
      field("far_price_ticks", arrow::int64(), false),
      field("near_price_ticks", arrow::int64(), false),
      field("ref_price_ticks", arrow::int64(), false),
      field("cross_type", arrow::utf8(), false),
      field("price_variation", arrow::utf8(), false),
      field("msg_type", arrow::utf8(), false),
      field("details", arrow::utf8(), true),
      field("seq", arrow::int64(), false),
  });
}

SchemaPtr instruments_schema() {
  return arrow::schema({
      field("venue", arrow::utf8(), false),
      field("symbol", arrow::utf8(), false),
      field("tick_size", arrow::float64(), false),
      field("lot_size", arrow::int32(), false),
      field("contract_type", arrow::utf8(), false),
      field("maker_fee_bps", arrow::float64(), true),
      field("taker_fee_bps", arrow::float64(), true),
      field("effective_from", arrow::date32(), false),
      field("effective_to", arrow::date32(), false),
      field("stock_locate", arrow::uint16(), false),
      field("market_category", arrow::utf8(), false),
      field("financial_status", arrow::utf8(), false),
      field("issue_classification", arrow::utf8(), false),
      field("issue_subtype", arrow::utf8(), false),
      field("authenticity", arrow::utf8(), false),
      field("luld_tier", arrow::utf8(), false),
  });
}

std::string one_char(char c) { return std::string(1, c); }

std::shared_ptr<arrow::Table> l3_table(const std::vector<L3Record>& rows, const std::string& venue,
                                       const std::string& symbol) {
  using Row = L3Record;
  const auto ts = column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ts; });
  return arrow::Table::Make(
      book_l3_schema(),
      {ts, ts, constant(rows, venue), constant(rows, symbol),
       column<arrow::UInt64Builder>(rows, [](const Row& r) { return r.order_id; }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return event_name(r.event); }),
       column<arrow::Int8Builder>(rows, [](const Row& r) { return r.side; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.price_ticks; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.qty; }),
       column<arrow::UInt64Builder>(rows,
                                    [](const Row& r) {
                                      return r.event == 'r' ? std::optional(r.new_order_id)
                                                            : std::nullopt;
                                    }),
       column<arrow::UInt64Builder>(
           rows,
           [](const Row& r) { return r.event == 'e' ? std::optional(r.match_id) : std::nullopt; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.seq; }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return one_char(r.msg_type); }),
       column<arrow::UInt16Builder>(rows, [](const Row& r) { return r.tracking_number; }),
       column<arrow::StringBuilder>(rows,
                                    [](const Row& r) {
                                      return r.mpid != 0 ? std::optional(mpid_text(r.mpid))
                                                         : std::nullopt;
                                    }),
       column<arrow::BooleanBuilder>(rows, [](const Row& r) {
         return r.printable != 0 ? std::optional(r.printable == 'Y') : std::nullopt;
       })});
}

std::shared_ptr<arrow::Table> trades_table(std::span<const TradeRecord> rows,
                                           const std::string& venue, const std::string& symbol) {
  using Row = TradeRecord;
  const auto ts = column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ts; });
  return arrow::Table::Make(
      trades_schema(),
      {ts, ts, constant(rows, venue), constant(rows, symbol),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return std::to_string(r.match_id); }),
       column<arrow::DoubleBuilder>(rows, [](const Row& r) { return dollars(r.price_ticks); }),
       column<arrow::DoubleBuilder>(rows, [](const Row& r) { return static_cast<double>(r.qty); }),
       column<arrow::Int8Builder>(rows, [](const Row& r) { return r.side; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.price_ticks; }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return one_char(r.source); }),
       column<arrow::StringBuilder>(rows,
                                    [](const Row& r) {
                                      return r.cross_type != 0
                                                 ? std::optional(one_char(r.cross_type))
                                                 : std::nullopt;
                                    }),
       column<arrow::BooleanBuilder>(rows, [](const Row& r) { return r.printable; }),
       column<arrow::BooleanBuilder>(rows, [](const Row& r) { return r.broken; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.seq; })});
}

std::shared_ptr<arrow::Table> bbo_table(const std::vector<BboRecord>& rows,
                                        const std::string& venue, const std::string& symbol) {
  using Row = BboRecord;
  return arrow::Table::Make(
      bbo_schema(), {column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ts; }),
                     constant(rows, venue), constant(rows, symbol),
                     column<arrow::DoubleBuilder>(rows,
                                                  [](const Row& r) {
                                                    return r.bid_ticks != 0
                                                               ? std::optional(dollars(r.bid_ticks))
                                                               : std::nullopt;
                                                  }),
                     column<arrow::DoubleBuilder>(rows,
                                                  [](const Row& r) {
                                                    return r.ask_ticks != 0
                                                               ? std::optional(dollars(r.ask_ticks))
                                                               : std::nullopt;
                                                  }),
                     column<arrow::DoubleBuilder>(
                         rows, [](const Row& r) { return static_cast<double>(r.bid_qty); }),
                     column<arrow::DoubleBuilder>(
                         rows, [](const Row& r) { return static_cast<double>(r.ask_qty); }),
                     column<arrow::Int64Builder>(rows, [](const Row& r) { return r.seq; }),
                     column<arrow::Int32Builder>(rows, [](const Row& r) { return r.bid_orders; }),
                     column<arrow::Int32Builder>(rows, [](const Row& r) { return r.ask_orders; })});
}

std::shared_ptr<arrow::Table> snapshots_table(const std::vector<SnapshotRecord>& rows,
                                              const std::string& venue, const std::string& symbol) {
  using Row = SnapshotRecord;
  return arrow::Table::Make(
      snapshots_schema(),
      {column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ts; }), constant(rows, venue),
       constant(rows, symbol),
       column<arrow::Int8Builder>(rows, [](const Row& r) { return r.side; }),
       column<arrow::UInt16Builder>(rows, [](const Row& r) { return r.level; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.price_ticks; }),
       column<arrow::DoubleBuilder>(rows, [](const Row& r) { return dollars(r.price_ticks); }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.qty; }),
       column<arrow::Int32Builder>(rows, [](const Row& r) { return r.order_count; })});
}

template <std::size_t N>
std::string text_of(const std::array<char, N>& chars) {
  return std::string(
      chars.data(),
      static_cast<std::size_t>(std::find(chars.begin(), chars.end(), '\0') - chars.begin()));
}

std::shared_ptr<arrow::Table> state_table(const std::vector<StateRecord>& rows,
                                          const std::string& venue, const std::string& symbol) {
  using Row = StateRecord;
  const auto ts = column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ts; });
  return arrow::Table::Make(
      trading_state_schema(),
      {ts, ts, constant(rows, venue),
       column<arrow::StringBuilder>(rows,
                                    [&symbol](const Row& r) {
                                      return r.locate != 0 ? std::optional(symbol) : std::nullopt;
                                    }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return one_char(r.msg_type); }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return one_char(r.state); }),
       column<arrow::StringBuilder>(rows,
                                    [](const Row& r) {
                                      return r.reason[0] != 0 ? std::optional(text_of(r.reason))
                                                              : std::nullopt;
                                    }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return text_of(r.details); }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.seq; })});
}

std::shared_ptr<arrow::Table> noii_table(const std::vector<NoiiRecord>& rows,
                                         const std::string& venue, const std::string& symbol) {
  using Row = NoiiRecord;
  const auto ts = column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ts; });
  return arrow::Table::Make(
      noii_schema(),
      {ts, ts, constant(rows, venue), constant(rows, symbol),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.paired_shares; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.imbalance_shares; }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return one_char(r.direction); }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.far_price_ticks; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.near_price_ticks; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ref_price_ticks; }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return one_char(r.cross_type); }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return one_char(r.price_variation); }),
       column<arrow::StringBuilder>(rows, [](const Row& r) { return one_char(r.msg_type); }),
       column<arrow::StringBuilder>(rows,
                                    [](const Row& r) {
                                      return r.details[0] != 0 ? std::optional(text_of(r.details))
                                                               : std::nullopt;
                                    }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.seq; })});
}

std::shared_ptr<arrow::Table> bars_table(std::span<const BarRecord> rows, const std::string& venue,
                                         const std::string& symbol, const std::string& interval) {
  using Row = BarRecord;
  return arrow::Table::Make(
      bars_schema(),
      {column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ts_start; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.ts_end; }),
       constant(rows, venue), constant(rows, symbol), constant(rows, interval),
       column<arrow::DoubleBuilder>(rows, [](const Row& r) { return dollars(r.open_ticks); }),
       column<arrow::DoubleBuilder>(rows, [](const Row& r) { return dollars(r.high_ticks); }),
       column<arrow::DoubleBuilder>(rows, [](const Row& r) { return dollars(r.low_ticks); }),
       column<arrow::DoubleBuilder>(rows, [](const Row& r) { return dollars(r.close_ticks); }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.volume; }),
       column<arrow::DoubleBuilder>(rows,
                                    [](const Row& r) {
                                      return static_cast<double>(r.notional_tick_shares) /
                                             kTicksPerDollar;
                                    }),
       column<arrow::DoubleBuilder>(rows,
                                    [](const Row& r) {
                                      return static_cast<double>(r.notional_tick_shares) /
                                             kTicksPerDollar / static_cast<double>(r.volume);
                                    }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.trade_count; }),
       column<arrow::Int64Builder>(rows, [](const Row& r) { return r.cross_volume; })});
}

/// The tick size under Reg NMS Rule 612 follows the price, so it is taken from the last trade: one
/// cent at $1.00 and above, 0.01 cent below. A symbol that did not trade gets the usual one cent.
double tick_size_from(std::span<const TradeRecord> rows) {
  constexpr std::int64_t kOneDollarTicks = 10000;
  return !rows.empty() && rows.back().price_ticks < kOneDollarTicks ? 0.0001 : 0.01;
}

std::shared_ptr<arrow::Table> instrument_table(const Instrument& instrument,
                                               std::span<const TradeRecord> trades,
                                               const SinkOptions& options,
                                               const std::string& symbol) {
  using std::chrono::sys_days;
  const std::array rows{&instrument};
  const auto days =
      sys_days{std::chrono::year{options.date.year} / std::chrono::month{options.date.month} /
               std::chrono::day{options.date.day}}
          .time_since_epoch()
          .count();
  const auto date_column = column<arrow::Date32Builder>(
      rows, [days](const Instrument*) { return static_cast<std::int32_t>(days); });
  const auto text = [&rows](auto value) {
    return column<arrow::StringBuilder>(
        rows, [value](const Instrument* item) { return std::string(value(*item)); });
  };
  const auto null_fee =
      column<arrow::DoubleBuilder>(rows, [](const Instrument*) { return std::optional<double>{}; });
  return arrow::Table::Make(
      instruments_schema(),
      {constant(rows, options.venue), constant(rows, symbol),
       column<arrow::DoubleBuilder>(rows,
                                    [&](const Instrument*) { return tick_size_from(trades); }),
       column<arrow::Int32Builder>(
           rows,
           [](const Instrument* item) { return static_cast<std::int32_t>(item->round_lot_size); }),
       text([](const Instrument& i) { return i.etp_flag == 'Y' ? "etp" : "equity"; }), null_fee,
       null_fee, date_column, date_column,
       column<arrow::UInt16Builder>(rows, [](const Instrument* item) { return item->locate; }),
       text([](const Instrument& i) { return one_char(i.market_category); }),
       text([](const Instrument& i) { return one_char(i.financial_status); }),
       text([](const Instrument& i) { return one_char(i.issue_classification); }),
       text([](const Instrument& i) {
         return std::string(i.issue_subtype.data(), i.issue_subtype.size());
       }),
       text([](const Instrument& i) { return one_char(i.authenticity); }),
       text([](const Instrument& i) { return one_char(i.luld_tier); })});
}

std::string interval_label(std::uint64_t interval_ns) {
  constexpr std::uint64_t kSecond = 1'000'000'000;
  return interval_ns % (60 * kSecond) == 0 ? std::to_string(interval_ns / (60 * kSecond)) + "m"
                                           : std::to_string(interval_ns / kSecond) + "s";
}

std::shared_ptr<parquet::WriterProperties> writer_properties(int compression_level,
                                                             std::int64_t row_group_rows) {
  return parquet::WriterProperties::Builder()
      .compression(parquet::Compression::ZSTD)
      ->compression_level(compression_level)
      ->max_row_group_length(row_group_rows)
      ->build();
}

/// One Parquet file of a partition, opened on first use, with one row group per write.
class PartitionFile {
 public:
  PartitionFile(std::filesystem::path path, SchemaPtr schema,
                std::shared_ptr<parquet::WriterProperties> properties)
      : path_(std::move(path)), schema_(std::move(schema)), properties_(std::move(properties)) {}

  void write(const std::shared_ptr<arrow::Table>& table) {
    if (!writer_) {  // First use. An empty partition never creates a file.
      std::filesystem::create_directories(path_.parent_path());
      file_ = ok(arrow::io::FileOutputStream::Open(path_.string()));
      writer_ = ok(parquet::arrow::FileWriter::Open(*schema_, arrow::default_memory_pool(), file_,
                                                    properties_));
    }
    ok(writer_->WriteTable(*table, table->num_rows()));
  }

  void close() {
    if (writer_) {
      ok(writer_->Close());
      ok(file_->Close());
      writer_.reset();
    }
  }

 private:
  std::filesystem::path path_;
  SchemaPtr schema_;
  std::shared_ptr<parquet::WriterProperties> properties_;
  std::shared_ptr<arrow::io::FileOutputStream> file_;
  std::unique_ptr<parquet::arrow::FileWriter> writer_;
};

/// Rows waiting for a row group, by locate, for one dataset.
template <class Record>
struct Buffers {
  std::unordered_map<std::uint16_t, std::vector<Record>> rows;
  std::map<std::uint16_t, PartitionFile> files;
};

}  // namespace

/// Everything on the writer thread's side. Only that thread touches it until the join in finish().
class Sinks::Writer {
 public:
  Writer(const SinkOptions& options, const std::vector<std::string>& symbols)
      : options_(options),
        symbols_(symbols),
        properties_(writer_properties(options.compression_level, options.row_group_rows)),
        date_(format_date(options.date)) {}

  void add(const L3Record& record) { append(l3_, record, "book_l3", book_l3_schema()); }
  void add(const BboRecord& record) { append(bbo_, record, "bbo", bbo_schema()); }
  void add(const SnapshotRecord& record) {
    append(snapshots_, record, "book_snapshots", snapshots_schema());
  }
  void add(const StateRecord& record) {
    append(state_, record, "trading_state", trading_state_schema());
  }
  void add(const NoiiRecord& record) { append(noii_, record, "noii", noii_schema()); }
  void add(const TradeRecord& record) { trades_[record.locate].push_back(record); }

  void flush_all() {
    flush_dataset(l3_, "book_l3", book_l3_schema());
    flush_dataset(bbo_, "bbo", bbo_schema());
    flush_dataset(snapshots_, "book_snapshots", snapshots_schema());
    flush_dataset(state_, "trading_state", trading_state_schema());
    flush_dataset(noii_, "noii", noii_schema());
  }

  std::unordered_map<std::uint16_t, std::vector<TradeRecord>>& trades() { return trades_; }

  [[nodiscard]] std::filesystem::path partition(const std::string& dataset,
                                                const std::string& symbol,
                                                const std::string& date) const {
    return options_.root / dataset / ("venue=" + options_.venue) / ("symbol=" + symbol) /
           ("date=" + date) / "part-0000.parquet";
  }

  [[nodiscard]] const std::shared_ptr<parquet::WriterProperties>& properties() const {
    return properties_;
  }
  [[nodiscard]] const std::string& date() const { return date_; }
  [[nodiscard]] std::string symbol_of(std::uint16_t locate) const {
    return locate == 0 ? "_MARKET" : symbols_[locate];
  }

 private:
  template <class Record>
  void append(Buffers<Record>& buffers, const Record& record, const char* dataset,
              const SchemaPtr& schema) {
    std::vector<Record>& rows = buffers.rows[record.locate];
    rows.push_back(record);
    if (static_cast<std::int64_t>(rows.size()) >= options_.row_group_rows) {
      flush_rows(buffers, record.locate, rows, dataset, schema);
    }
  }

  template <class Record>
  void flush_rows(Buffers<Record>& buffers, std::uint16_t locate, std::vector<Record>& rows,
                  const char* dataset, const SchemaPtr& schema) {
    if (rows.empty()) {
      return;
    }
    const std::string symbol = symbol_of(locate);
    auto file = buffers.files.find(locate);
    if (file == buffers.files.end()) {
      file = buffers.files
                 .emplace(locate,
                          PartitionFile(partition(dataset, symbol, date_), schema, properties_))
                 .first;
    }
    file->second.write(make_table(rows, symbol));
    rows.clear();
  }

  template <class Record>
  void flush_dataset(Buffers<Record>& buffers, const char* dataset, const SchemaPtr& schema) {
    for (auto& [locate, rows] : buffers.rows) {
      flush_rows(buffers, locate, rows, dataset, schema);
    }
    for (auto& [locate, file] : buffers.files) {
      file.close();
    }
  }

  std::shared_ptr<arrow::Table> make_table(const std::vector<L3Record>& rows,
                                           const std::string& symbol) {
    return l3_table(rows, options_.venue, symbol);
  }
  std::shared_ptr<arrow::Table> make_table(const std::vector<BboRecord>& rows,
                                           const std::string& symbol) {
    return bbo_table(rows, options_.venue, symbol);
  }
  std::shared_ptr<arrow::Table> make_table(const std::vector<SnapshotRecord>& rows,
                                           const std::string& symbol) {
    return snapshots_table(rows, options_.venue, symbol);
  }
  std::shared_ptr<arrow::Table> make_table(const std::vector<StateRecord>& rows,
                                           const std::string& symbol) {
    return state_table(rows, options_.venue, symbol);
  }
  std::shared_ptr<arrow::Table> make_table(const std::vector<NoiiRecord>& rows,
                                           const std::string& symbol) {
    return noii_table(rows, options_.venue, symbol);
  }

  const SinkOptions& options_;
  const std::vector<std::string>& symbols_;
  std::shared_ptr<parquet::WriterProperties> properties_;
  std::string date_;
  Buffers<L3Record> l3_;
  Buffers<BboRecord> bbo_;
  Buffers<SnapshotRecord> snapshots_;
  Buffers<StateRecord> state_;
  Buffers<NoiiRecord> noii_;
  std::unordered_map<std::uint16_t, std::vector<TradeRecord>> trades_;
};

unsigned dataset_mask(const std::vector<std::string>& names) {
  static const std::map<std::string, Dataset> kNames{
      {"book_l3", Dataset::BookL3},
      {"trades", Dataset::Trades},
      {"bbo", Dataset::Bbo},
      {"bars", Dataset::Bars},
      {"book_snapshots", Dataset::Snapshots},
      {"trading_state", Dataset::TradingState},
      {"noii", Dataset::Noii},
      {"instruments", Dataset::Instruments},
  };
  unsigned mask = 0;
  for (const std::string& name : names) {
    const auto found = kNames.find(name);
    if (found == kNames.end()) {
      throw std::invalid_argument("unknown dataset: " + name);
    }
    mask |= static_cast<unsigned>(found->second);
  }
  return mask;
}

Sinks::Sinks(SinkOptions options)
    : options_(std::move(options)),
      day_(options_.date),
      wanted_(LocateTable::kSize, 0),
      wanted_symbols_(LocateTable::kSize),
      l3_(options_.queue_capacity),
      trades_(options_.queue_capacity),
      bbo_(options_.queue_capacity),
      snapshots_(options_.queue_capacity),
      state_(options_.queue_capacity),
      noii_(options_.queue_capacity),
      writer_(std::make_unique<Writer>(options_, wanted_symbols_)) {
  thread_ = std::thread([this] { run_writer(); });
}

Sinks::~Sinks() {
  finishing_.store(true, std::memory_order_release);
  if (thread_.joinable()) {
    thread_.join();
  }
}

void Sinks::define(const Instrument& instrument) {
  const std::string symbol(instrument.symbol());
  const bool selected =
      options_.all_symbols ||
      std::find(options_.symbols.begin(), options_.symbols.end(), symbol) != options_.symbols.end();
  if (selected && wanted_[instrument.locate] == 0) {
    // The writer thread reads this table only for locates it has records for, and each record
    // follows this write through a queue with release and acquire ordering.
    wanted_symbols_[instrument.locate] = symbol;
    wanted_[instrument.locate] = 1;
    wanted_locates_.push_back(instrument.locate);
  }
}

void Sinks::run_writer() {
  L3Record l3;
  TradeRecord trade;
  BboRecord bbo;
  SnapshotRecord snapshot;
  StateRecord state;
  NoiiRecord noii;
  constexpr int kBatch = 4096;

  for (;;) {
    const bool last_pass = finishing_.load(std::memory_order_acquire);
    bool idle = true;
    const auto drain = [&](auto& queue, auto& record) {
      for (int i = 0; i < kBatch && queue.try_pop(record); ++i) {
        writer_->add(record);
        idle = false;
      }
    };
    drain(l3_, l3);
    drain(trades_, trade);
    drain(bbo_, bbo);
    drain(snapshots_, snapshot);
    drain(state_, state);
    drain(noii_, noii);
    if (idle) {
      if (last_pass) {
        return;
      }
      std::this_thread::yield();
    }
  }
}

void Sinks::finish(const std::vector<std::uint64_t>& broken_matches,
                   const LocateTable& instruments) {
  finishing_.store(true, std::memory_order_release);
  thread_.join();

  const bool write_files = !options_.root.empty();
  if (write_files) {
    writer_->flush_all();
  }

  // Correction pass: FR-10. Broken trades stay in the dataset, flagged, and leave volume and bars.
  const std::unordered_set<std::uint64_t> broken(broken_matches.begin(), broken_matches.end());
  for (auto& [locate, rows] : writer_->trades()) {
    for (TradeRecord& row : rows) {
      row.broken = broken.contains(row.match_id);
    }
  }
  if (!write_files) {
    return;
  }

  for (const std::uint16_t locate : wanted_locates_) {
    const std::span<const TradeRecord> rows = trades(locate);
    const std::string symbol = wanted_symbols_[locate];
    if (enabled(Dataset::Trades) && !rows.empty()) {
      PartitionFile file(writer_->partition("trades", symbol, writer_->date()), trades_schema(),
                         writer_->properties());
      file.write(trades_table(rows, options_.venue, symbol));
      file.close();
    }
    if (enabled(Dataset::Bars)) {
      PartitionFile file(writer_->partition("bars", symbol, writer_->date()), bars_schema(),
                         writer_->properties());
      for (const std::uint64_t interval : options_.bar_intervals_ns) {
        const auto bars =
            build_bars(rows, day_, {interval, options_.bars_rth_only, options_.bars_include_cross});
        if (!bars.empty()) {
          file.write(bars_table(bars, options_.venue, symbol, interval_label(interval)));
        }
      }
      file.close();
    }
    if (enabled(Dataset::Instruments)) {
      PartitionFile file(writer_->partition("instruments", symbol, writer_->date()),
                         instruments_schema(), writer_->properties());
      file.write(instrument_table(instruments[locate], rows, options_, symbol));
      file.close();
    }
  }
}

std::span<const TradeRecord> Sinks::trades(std::uint16_t locate) const noexcept {
  const auto found = writer_->trades().find(locate);
  if (found == writer_->trades().end()) {
    return {};
  }
  return found->second;
}

SymbolVolume Sinks::volume(std::uint16_t locate) const noexcept {
  SymbolVolume out;
  for (const TradeRecord& row : trades(locate)) {
    if (row.broken && row.printable) {
      out.broken_shares += row.qty;
    }
    if (counts_toward_volume(row)) {
      out.volume += row.qty;
    }
    if (row.cross_type == 'C') {
      out.closing_cross_shares += row.qty;
      out.closing_cross_ticks = row.price_ticks;
    }
  }
  return out;
}

void write_bars_file(const std::filesystem::path& path, std::span<const BarRecord> bars,
                     const std::string& venue, const std::string& symbol,
                     const std::string& interval) {
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  PartitionFile file(path, bars_schema(), writer_properties(3, 1'000'000));
  file.write(bars_table(bars, venue, symbol, interval));
  file.close();
}

}  // namespace itch
