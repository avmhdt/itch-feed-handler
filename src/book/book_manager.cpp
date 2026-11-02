#include "itch/book/book_manager.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "itch/util/digest.hpp"

namespace itch {

namespace {

constexpr std::size_t kInitialLevels = 16;
constexpr std::size_t kMaxBrokenMatches = 4096;

constexpr Side side_of(char indicator) noexcept { return indicator == 'B' ? Side::Bid : Side::Ask; }

std::uint32_t pack_mpid(std::string_view attribution) noexcept {
  std::uint32_t packed = 0;
  for (const char c : attribution) {
    packed = (packed << 8U) | static_cast<unsigned char>(c);
  }
  return packed;
}

}  // namespace

BookManager::BookManager(const BookConfig& config, Sinks* sinks)
    : store_(config.order_map, config.order_capacity_hint),
      books_(LocateTable::kSize, OrderBook(store_)),
      symbol_counters_(LocateTable::kSize),
      settling_since_ns_(LocateTable::kSize, 0),
      sinks_(sinks) {
  broken_matches_.reserve(kMaxBrokenMatches);
  if (sinks_ != nullptr) {
    last_bbo_.resize(LocateTable::kSize);
    if (sinks_->enabled(Dataset::Snapshots)) {
      next_snapshot_ns_ = sinks_->options().snapshot_interval_ns;
    }
  }
}

const OrderBook* BookManager::book(std::string_view symbol) const noexcept {
  const std::uint16_t locate = instruments_.find(symbol);
  return locate == 0 ? nullptr : &books_[locate];
}

void BookManager::on(const SystemEvent& message) noexcept {
  note_time(message.timestamp_ns());
  state_.on(message);
  emit_state(0, message.timestamp_ns(), 'S', message.event_code(), {}, "{}");
  ++seq_;
}

void BookManager::on(const StockDirectory& message) {
  note_time(message.timestamp_ns());
  instruments_.define(message);
  books_[message.stock_locate()].reserve(kInitialLevels);
  if (sinks_ != nullptr) {
    sinks_->define(instruments_[message.stock_locate()]);
  }
  ++seq_;
}

void BookManager::on(const TradingAction& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  state_.on(message);
  emit_state(message.stock_locate(), message.timestamp_ns(), 'H', message.trading_state(),
             trim_right(message.reason()), "{}");
  ++seq_;
}

void BookManager::on(const RegSho& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  state_.on(message);
  emit_state(message.stock_locate(), message.timestamp_ns(), 'Y', message.action(), {}, "{}");
  ++seq_;
}

void BookManager::on(const ParticipantPosition& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  if (out(message.stock_locate())) {
    std::array<char, 80> details{};
    std::snprintf(details.data(), details.size(),
                  R"({"mpid":"%.4s","primary_market_maker":"%c","market_maker_mode":"%c"})",
                  message.mpid().data(), message.primary_market_maker(),
                  message.market_maker_mode());
    emit_state(message.stock_locate(), message.timestamp_ns(), 'L', message.participant_state(), {},
               details.data());
  }
  ++seq_;
}

void BookManager::on(const MwcbDecline& message) noexcept {
  note_time(message.timestamp_ns());
  if (sinks_ != nullptr) {
    std::array<char, 100> details{};
    std::snprintf(details.data(), details.size(), R"({"level1":%llu,"level2":%llu,"level3":%llu})",
                  static_cast<unsigned long long>(message.level1()),
                  static_cast<unsigned long long>(message.level2()),
                  static_cast<unsigned long long>(message.level3()));
    emit_state(0, message.timestamp_ns(), 'V', ' ', {}, details.data());
  }
  ++seq_;
}

void BookManager::on(const MwcbStatus& message) noexcept {
  note_time(message.timestamp_ns());
  emit_state(0, message.timestamp_ns(), 'W', message.breached_level(), {}, "{}");
  ++seq_;
}

void BookManager::on(const IpoQuoting& message) noexcept {
  note_time(message.timestamp_ns());
  if (out(message.stock_locate())) {
    std::array<char, 80> details{};
    std::snprintf(details.data(), details.size(), R"({"release_time_s":%u,"ipo_price":%u})",
                  message.release_time_s(), message.ipo_price());
    emit_state(message.stock_locate(), message.timestamp_ns(), 'K', message.release_qualifier(), {},
               details.data());
  }
  ++seq_;
}

void BookManager::on(const LuldCollar& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  if (out(message.stock_locate())) {
    std::array<char, 100> details{};
    std::snprintf(details.data(), details.size(),
                  R"({"reference":%u,"upper":%u,"lower":%u,"extension":%u})",
                  message.reference_price(), message.upper_price(), message.lower_price(),
                  message.extension());
    emit_state(message.stock_locate(), message.timestamp_ns(), 'J', ' ', {}, details.data());
  }
  ++seq_;
}

void BookManager::on(const OperationalHalt& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  state_.on(message);
  if (out(message.stock_locate())) {
    std::array<char, 32> details{};
    std::snprintf(details.data(), details.size(), R"({"market_code":"%c"})", message.market_code());
    emit_state(message.stock_locate(), message.timestamp_ns(), 'h', message.action(), {},
               details.data());
  }
  ++seq_;
}

void BookManager::add_order(const AddOrder& message, std::uint32_t mpid) {
  const std::uint16_t locate = message.stock_locate();
  note_time(message.timestamp_ns());
  check_locate(locate);
  const std::uint32_t index = store_.allocate(message.order_ref());
  if (index == kNoOrder) [[unlikely]] {
    ++counters_.duplicate_adds;
    ++seq_;
    return;
  }
  OrderNode& node = store_[index];
  node.price = message.price();
  node.shares = message.shares();
  node.mpid = mpid;
  node.locate = locate;
  node.side = side_of(message.side());
  books_[locate].link(index);
  symbol_counters_[locate].added += message.shares();
  check_book(locate, message.timestamp_ns());
  if (out(locate)) {
    emit_l3({.ts = sinks_->day().to_utc_ns(message.timestamp_ns()),
             .seq = static_cast<std::int64_t>(seq_),
             .price_ticks = message.price(),
             .qty = message.shares(),
             .order_id = message.order_ref(),
             .mpid = mpid,
             .tracking_number = message.tracking_number(),
             .locate = locate,
             .side = static_cast<std::int8_t>(node.side),
             .event = 'a',
             .msg_type = static_cast<char>(message.type())});
    emit_bbo(locate, message.timestamp_ns());
  }
  ++seq_;
}

void BookManager::on(const AddOrder& message) { add_order(message, 0); }

void BookManager::on(const AddOrderMpid& message) {
  add_order(message, pack_mpid(message.attribution()));
}

std::uint32_t BookManager::take_shares(std::uint32_t index, std::uint32_t shares) noexcept {
  const OrderNode& node = store_[index];
  std::uint32_t taken = shares;
  if (shares > node.shares) [[unlikely]] {
    ++counters_.over_executions;
    taken = node.shares;
  }
  books_[node.locate].reduce(index, taken);
  return taken;
}

void BookManager::on(const OrderExecuted& message) {
  note_time(message.timestamp_ns());
  const std::uint32_t index = store_.find(message.order_ref());
  if (index == kNoOrder) [[unlikely]] {
    ++counters_.orphans;
    ++seq_;
    return;
  }
  const OrderNode resting = store_[index];
  const std::uint16_t locate = resting.locate;
  const std::uint32_t taken = take_shares(index, message.executed_shares());
  SymbolCounters& counters = symbol_counters_[locate];
  counters.executed += taken;
  counters.volume += taken;
  check_book(locate, message.timestamp_ns());
  if (out(locate)) {
    emit_exec(resting, message.order_ref(), taken, message.match_number(), resting.price, 'Y', 'E',
              message.tracking_number(), message.timestamp_ns());
  }
  ++seq_;
}

void BookManager::on(const OrderExecutedPrice& message) {
  note_time(message.timestamp_ns());
  const std::uint32_t index = store_.find(message.order_ref());
  if (index == kNoOrder) [[unlikely]] {
    ++counters_.orphans;
    ++seq_;
    return;
  }
  const OrderNode resting = store_[index];
  const std::uint16_t locate = resting.locate;
  const std::uint32_t taken = take_shares(index, message.executed_shares());
  SymbolCounters& counters = symbol_counters_[locate];
  counters.executed += taken;
  // ITCH 5.0 1.4.2: non-printable executions are part of a bulk print and count only there.
  if (message.printable() == 'Y') {
    counters.volume += taken;
  }
  check_book(locate, message.timestamp_ns());
  if (out(locate)) {
    emit_exec(resting, message.order_ref(), taken, message.match_number(),
              message.execution_price(), message.printable(), 'C', message.tracking_number(),
              message.timestamp_ns());
  }
  ++seq_;
}

void BookManager::on(const OrderCancel& message) {
  note_time(message.timestamp_ns());
  const std::uint32_t index = store_.find(message.order_ref());
  if (index == kNoOrder) [[unlikely]] {
    ++counters_.orphans;
    ++seq_;
    return;
  }
  const OrderNode resting = store_[index];
  const std::uint16_t locate = resting.locate;
  const std::uint32_t taken = take_shares(index, message.cancelled_shares());
  symbol_counters_[locate].cancelled += taken;
  check_book(locate, message.timestamp_ns());
  if (out(locate)) {
    emit_l3({.ts = sinks_->day().to_utc_ns(message.timestamp_ns()),
             .seq = static_cast<std::int64_t>(seq_),
             .price_ticks = resting.price,
             .qty = taken,
             .order_id = message.order_ref(),
             .tracking_number = message.tracking_number(),
             .locate = locate,
             .side = static_cast<std::int8_t>(resting.side),
             .event = 'c',
             .msg_type = 'X'});
    emit_bbo(locate, message.timestamp_ns());
  }
  ++seq_;
}

void BookManager::on(const OrderDelete& message) {
  note_time(message.timestamp_ns());
  const std::uint32_t index = store_.find(message.order_ref());
  if (index == kNoOrder) [[unlikely]] {
    ++counters_.orphans;
    ++seq_;
    return;
  }
  const OrderNode resting = store_[index];
  const std::uint16_t locate = resting.locate;
  symbol_counters_[locate].deleted += resting.shares;
  books_[locate].unlink(index);
  store_.release(index);
  check_book(locate, message.timestamp_ns());
  if (out(locate)) {
    emit_l3({.ts = sinks_->day().to_utc_ns(message.timestamp_ns()),
             .seq = static_cast<std::int64_t>(seq_),
             .price_ticks = resting.price,
             .qty = resting.shares,
             .order_id = message.order_ref(),
             .tracking_number = message.tracking_number(),
             .locate = locate,
             .side = static_cast<std::int8_t>(resting.side),
             .event = 'd',
             .msg_type = 'D'});
    emit_bbo(locate, message.timestamp_ns());
  }
  ++seq_;
}

void BookManager::on(const OrderReplace& message) {
  note_time(message.timestamp_ns());
  const std::uint32_t index = store_.find(message.original_ref());
  if (index == kNoOrder) [[unlikely]] {
    ++counters_.orphans;
    ++seq_;
    return;
  }
  const OrderNode resting = store_[index];
  const std::uint16_t locate = resting.locate;
  OrderBook& book = books_[locate];
  SymbolCounters& counters = symbol_counters_[locate];
  counters.replaced_out += resting.shares;
  if (!store_.rename(index, message.new_ref())) [[unlikely]] {
    // DQ-04: the new reference is already live, so the replacement cannot be represented. The
    // shares of the original still leave the book, which keeps the accounting identity.
    ++counters_.duplicate_adds;
    book.unlink(index);
    store_.release(index);
  } else {
    book.relink(index, message.price(), message.shares());
    counters.added += message.shares();
  }
  check_book(locate, message.timestamp_ns());
  if (out(locate)) {
    emit_l3({.ts = sinks_->day().to_utc_ns(message.timestamp_ns()),
             .seq = static_cast<std::int64_t>(seq_),
             .price_ticks = message.price(),
             .qty = message.shares(),
             .order_id = message.original_ref(),
             .new_order_id = message.new_ref(),
             .mpid = resting.mpid,
             .tracking_number = message.tracking_number(),
             .locate = locate,
             .side = static_cast<std::int8_t>(resting.side),
             .event = 'r',
             .msg_type = 'U'});
    emit_bbo(locate, message.timestamp_ns());
  }
  ++seq_;
}

void BookManager::on(const Trade& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  symbol_counters_[message.stock_locate()].volume += message.shares();
  if (out(message.stock_locate()) && sinks_->enabled(Dataset::Trades)) {
    sinks_->push(TradeRecord{.ts = sinks_->day().to_utc_ns(message.timestamp_ns()),
                             .seq = static_cast<std::int64_t>(seq_),
                             .price_ticks = message.price(),
                             .qty = message.shares(),
                             .match_id = message.match_number(),
                             .locate = message.stock_locate(),
                             .source = 'P'});
  }
  ++seq_;
}

void BookManager::on(const CrossTrade& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  SymbolCounters& counters = symbol_counters_[message.stock_locate()];
  const auto shares = static_cast<std::int64_t>(message.shares());
  counters.volume += shares;
  counters.cross_volume += shares;
  state_.on(message);
  // A book still crossed at the print is unwinding this cross; see docs/protocol-notes.md#crosses.
  if (books_[message.stock_locate()].touching()) {
    settling_since_ns_[message.stock_locate()] = message.timestamp_ns() + 1;
  }
  if (out(message.stock_locate()) && sinks_->enabled(Dataset::Trades)) {
    sinks_->push(TradeRecord{.ts = sinks_->day().to_utc_ns(message.timestamp_ns()),
                             .seq = static_cast<std::int64_t>(seq_),
                             .price_ticks = message.cross_price(),
                             .qty = shares,
                             .match_id = message.match_number(),
                             .locate = message.stock_locate(),
                             .source = 'Q',
                             .cross_type = message.cross_type()});
  }
  ++seq_;
}

void BookManager::on(const BrokenTrade& message) noexcept {
  note_time(message.timestamp_ns());
  ++counters_.broken_trades;
  if (broken_matches_.size() < kMaxBrokenMatches) {
    broken_matches_.push_back(message.match_number());
  }
  ++seq_;
}

void BookManager::on(const Noii& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  state_.on(message);
  if (out(message.stock_locate()) && sinks_->enabled(Dataset::Noii)) {
    sinks_->push(
        NoiiRecord{.ts = sinks_->day().to_utc_ns(message.timestamp_ns()),
                   .seq = static_cast<std::int64_t>(seq_),
                   .paired_shares = static_cast<std::int64_t>(message.paired_shares()),
                   .imbalance_shares = static_cast<std::int64_t>(message.imbalance_shares()),
                   .far_price_ticks = message.far_price(),
                   .near_price_ticks = message.near_price(),
                   .ref_price_ticks = message.reference_price(),
                   .locate = message.stock_locate(),
                   .msg_type = 'I',
                   .direction = message.imbalance_direction(),
                   .cross_type = message.cross_type(),
                   .price_variation = message.price_variation()});
  }
  ++seq_;
}

void BookManager::on(const Rpii& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  emit_state(message.stock_locate(), message.timestamp_ns(), 'N', message.interest_flag(), {},
             "{}");
  ++seq_;
}

void BookManager::on(const Dlcr& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  if (out(message.stock_locate()) && sinks_->enabled(Dataset::Noii)) {
    NoiiRecord record{.ts = sinks_->day().to_utc_ns(message.timestamp_ns()),
                      .seq = static_cast<std::int64_t>(seq_),
                      .ref_price_ticks = message.near_execution_price(),
                      .locate = message.stock_locate(),
                      .msg_type = 'O'};
    std::snprintf(record.details.data(), record.details.size(),
                  R"({"eligible":"%c","min":%u,"max":%u,"lower":%u,"upper":%u,"near_time":%llu})",
                  message.open_eligibility(), message.minimum_allowable_price(),
                  message.maximum_allowable_price(), message.lower_price_collar(),
                  message.upper_price_collar(),
                  static_cast<unsigned long long>(message.near_execution_time()));
    sinks_->push(record);
  }
  ++seq_;
}

void BookManager::emit_l3(const L3Record& record) noexcept {
  if (sinks_->enabled(Dataset::BookL3)) {
    sinks_->push(record);
  }
}

void BookManager::emit_exec(const OrderNode& node, std::uint64_t ref, std::uint32_t shares,
                            std::uint64_t match, std::int64_t price, char printable, char msg_type,
                            std::uint16_t tracking, std::uint64_t timestamp_ns) noexcept {
  const std::int64_t ts = sinks_->day().to_utc_ns(timestamp_ns);
  emit_l3({.ts = ts,
           .seq = static_cast<std::int64_t>(seq_),
           .price_ticks = price,
           .qty = shares,
           .order_id = ref,
           .match_id = match,
           .tracking_number = tracking,
           .locate = node.locate,
           .side = static_cast<std::int8_t>(node.side),
           .event = 'e',
           .msg_type = msg_type,
           .printable = printable});
  if (sinks_->enabled(Dataset::Trades)) {
    // The aggressor is on the other side of the resting order.
    sinks_->push(TradeRecord{.ts = ts,
                             .seq = static_cast<std::int64_t>(seq_),
                             .price_ticks = price,
                             .qty = shares,
                             .match_id = match,
                             .locate = node.locate,
                             .side = static_cast<std::int8_t>(-static_cast<int>(node.side)),
                             .source = msg_type,
                             .printable = printable == 'Y'});
  }
  emit_bbo(node.locate, timestamp_ns);
}

void BookManager::emit_bbo(std::uint16_t locate, std::uint64_t timestamp_ns) noexcept {
  if (!sinks_->enabled(Dataset::Bbo)) {
    return;
  }
  const auto bid = books_[locate].best(Side::Bid).value_or(Level{0, 0, 0});
  const auto ask = books_[locate].best(Side::Ask).value_or(Level{0, 0, 0});
  BboRecord& last = last_bbo_[locate];
  if (bid.price_ticks == last.bid_ticks && bid.qty == last.bid_qty &&
      ask.price_ticks == last.ask_ticks && ask.qty == last.ask_qty) {
    return;
  }
  last = BboRecord{.ts = sinks_->day().to_utc_ns(timestamp_ns),
                   .seq = static_cast<std::int64_t>(seq_),
                   .bid_ticks = bid.price_ticks,
                   .ask_ticks = ask.price_ticks,
                   .bid_qty = bid.qty,
                   .ask_qty = ask.qty,
                   .bid_orders = bid.orders,
                   .ask_orders = ask.orders,
                   .locate = locate};
  sinks_->push(last);
}

void BookManager::emit_state(std::uint16_t locate, std::uint64_t timestamp_ns, char msg_type,
                             char state, std::string_view reason, const char* details) noexcept {
  if (sinks_ == nullptr || !sinks_->enabled(Dataset::TradingState) ||
      (locate != 0 && !sinks_->wants(locate))) {
    return;
  }
  StateRecord record{.ts = sinks_->day().to_utc_ns(timestamp_ns),
                     .seq = static_cast<std::int64_t>(seq_),
                     .locate = locate,
                     .msg_type = msg_type,
                     .state = state};
  std::copy_n(reason.begin(), std::min(reason.size(), record.reason.size()), record.reason.begin());
  std::snprintf(record.details.data(), record.details.size(), "%s", details);
  sinks_->push(record);
}

void BookManager::take_snapshots(std::uint64_t timestamp_ns) noexcept {
  // DQ-11: a snapshot at T holds everything up to and including T, so it is taken when the first
  // message after T arrives and before that message is applied.
  std::array<Level, 64> levels{};
  const std::uint64_t interval = sinks_->options().snapshot_interval_ns;
  const std::size_t depth = std::min(sinks_->options().snapshot_depth, levels.size());
  while (timestamp_ns > next_snapshot_ns_) {
    // Nothing rests before the first order, so the empty early boundaries write no rows.
    const std::int64_t ts = sinks_->day().to_utc_ns(next_snapshot_ns_);
    for (const std::uint16_t locate : sinks_->wanted_locates()) {
      for (const Side side : {Side::Bid, Side::Ask}) {
        const std::size_t count = books_[locate].levels(side, std::span(levels).first(depth));
        for (std::size_t i = 0; i < count; ++i) {
          sinks_->push(SnapshotRecord{.ts = ts,
                                      .price_ticks = levels[i].price_ticks,
                                      .qty = levels[i].qty,
                                      .order_count = levels[i].orders,
                                      .level = static_cast<std::uint16_t>(i),
                                      .locate = locate,
                                      .side = static_cast<std::int8_t>(side)});
        }
      }
    }
    next_snapshot_ns_ += interval;
  }
}

void BookManager::check_book(std::uint16_t locate, std::uint64_t timestamp_ns) noexcept {
  const OrderBook& book = books_[locate];
  if (book.touching()) [[unlikely]] {
    classify_touch(book, locate, timestamp_ns);
  } else if (settling_since_ns_[locate] != 0) [[unlikely]] {
    settling_since_ns_[locate] = 0;
  }
}

void BookManager::classify_touch(const OrderBook& book, std::uint16_t locate,
                                 std::uint64_t timestamp_ns) noexcept {
  const bool crossed = book.crossed();
  const SymbolState& symbol = state_[locate];
  const bool continuous = state_.system_open() && symbol.trading_state == 'T' &&
                          !state_.in_cross_window(locate, instruments_[locate], timestamp_ns);
  if (!continuous) {
    ++(crossed ? counters_.crossed_in_auction : counters_.locked_in_auction);
    return;
  }
  // Only what would otherwise be a violation reaches this test, so the longest settlement measures
  // exactly what the rule excuses.
  const std::uint64_t settling_since = settling_since_ns_[locate];
  if (settling_since != 0) {
    ++(crossed ? counters_.crossed_in_settlement : counters_.locked_in_settlement);
    const std::uint64_t elapsed = timestamp_ns + 1 - settling_since;
    if (elapsed > counters_.longest_settlement_ns) {
      counters_.longest_settlement_ns = elapsed;
      counters_.longest_settlement_locate = locate;
    }
    return;
  }
  SymbolCounters& per_symbol = symbol_counters_[locate];
  if (!crossed) {
    ++counters_.locked_outside_auction;
    ++per_symbol.locked_outside;
    return;
  }
  ++counters_.crossed_outside_auction;
  ++per_symbol.crossed_outside;
  if (counters_.crossed_sample_count < Counters::kMaxSamples) {
    counters_.crossed_samples[counters_.crossed_sample_count++] = {seq_, locate};
  }
}

std::int64_t BookManager::accounting_gap(std::uint16_t locate) const noexcept {
  const SymbolCounters& counters = symbol_counters_[locate];
  std::int64_t resting = 0;
  const auto count = [&resting](const PriceLevel& level) { resting += level.qty; };
  books_[locate].for_each_level(Side::Bid, count);
  books_[locate].for_each_level(Side::Ask, count);
  return counters.added - counters.executed - counters.cancelled - counters.deleted -
         counters.replaced_out - resting;
}

std::uint64_t BookManager::digest() const noexcept {
  Digest digest;
  const auto hash_order = [&digest](const OrderNode& node) {
    digest.add(node.ref);
    digest.add(node.shares);
  };
  const auto hash_level = [&digest](const PriceLevel& level) {
    digest.add(level.price);
    digest.add(static_cast<std::uint64_t>(level.qty));
    digest.add(static_cast<std::uint64_t>(level.orders));
  };
  for (const Instrument* instrument : instruments_.all()) {
    const OrderBook& book = books_[instrument->locate];
    const SymbolCounters& counters = symbol_counters_[instrument->locate];
    digest.add(instrument->locate);
    for (const Side side : {Side::Bid, Side::Ask}) {
      book.for_each_level(side, hash_level);
      book.for_each_order(side, hash_order);
    }
    for (const std::int64_t value :
         {counters.added, counters.executed, counters.cancelled, counters.deleted,
          counters.replaced_out, counters.volume, counters.cross_volume}) {
      digest.add(static_cast<std::uint64_t>(value));
    }
  }
  digest.add(seq_);
  digest.add(counters_.orphans);
  digest.add(counters_.duplicate_adds);
  digest.add(counters_.over_executions);
  digest.add(counters_.undefined_locates);
  digest.add(counters_.broken_trades);
  return digest.value();
}

void BookManager::reset() noexcept {
  store_.clear();
  for (OrderBook& book : books_) {
    book.clear();
  }
  std::fill(symbol_counters_.begin(), symbol_counters_.end(), SymbolCounters{});
  std::fill(settling_since_ns_.begin(), settling_since_ns_.end(), 0);
  instruments_.clear();
  state_.reset();
  counters_ = Counters{};
  broken_matches_.clear();
  std::fill(last_bbo_.begin(), last_bbo_.end(), BboRecord{});
  seq_ = 0;
  last_timestamp_ = 0;
}

}  // namespace itch
