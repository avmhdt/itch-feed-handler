#include "itch/book/book_manager.hpp"

#include <algorithm>
#include <array>

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
      sinks_(sinks) {
  broken_matches_.reserve(kMaxBrokenMatches);
}

const OrderBook* BookManager::book(std::string_view symbol) const noexcept {
  const std::uint16_t locate = instruments_.find(symbol);
  return locate == 0 ? nullptr : &books_[locate];
}

void BookManager::on(const SystemEvent& message) noexcept {
  note_time(message.timestamp_ns());
  state_.on(message);
  ++seq_;
}

void BookManager::on(const StockDirectory& message) {
  note_time(message.timestamp_ns());
  instruments_.define(message);
  books_[message.stock_locate()].reserve(kInitialLevels);
  ++seq_;
}

void BookManager::on(const TradingAction& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  state_.on(message);
  ++seq_;
}

void BookManager::on(const RegSho& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  state_.on(message);
  ++seq_;
}

void BookManager::on(const ParticipantPosition& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  ++seq_;
}

void BookManager::on(const MwcbDecline& message) noexcept {
  note_time(message.timestamp_ns());
  ++seq_;
}

void BookManager::on(const MwcbStatus& message) noexcept {
  note_time(message.timestamp_ns());
  ++seq_;
}

void BookManager::on(const IpoQuoting& message) noexcept {
  note_time(message.timestamp_ns());
  ++seq_;
}

void BookManager::on(const LuldCollar& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  ++seq_;
}

void BookManager::on(const OperationalHalt& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  state_.on(message);
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
  const std::uint16_t locate = store_[index].locate;
  const std::uint32_t taken = take_shares(index, message.executed_shares());
  SymbolCounters& counters = symbol_counters_[locate];
  counters.executed += taken;
  counters.volume += taken;
  check_book(locate, message.timestamp_ns());
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
  const std::uint16_t locate = store_[index].locate;
  const std::uint32_t taken = take_shares(index, message.executed_shares());
  SymbolCounters& counters = symbol_counters_[locate];
  counters.executed += taken;
  // ITCH 5.0 1.4.2: non-printable executions are part of a later bulk print.
  if (message.printable() == 'Y') {
    counters.volume += taken;
  }
  check_book(locate, message.timestamp_ns());
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
  const std::uint16_t locate = store_[index].locate;
  symbol_counters_[locate].cancelled += take_shares(index, message.cancelled_shares());
  check_book(locate, message.timestamp_ns());
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
  const OrderNode& node = store_[index];
  const std::uint16_t locate = node.locate;
  symbol_counters_[locate].deleted += node.shares;
  books_[locate].unlink(index);
  store_.release(index);
  check_book(locate, message.timestamp_ns());
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
  const OrderNode& node = store_[index];
  const std::uint16_t locate = node.locate;
  OrderBook& book = books_[locate];
  SymbolCounters& counters = symbol_counters_[locate];
  counters.replaced_out += node.shares;
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
  ++seq_;
}

void BookManager::on(const Trade& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  symbol_counters_[message.stock_locate()].volume += message.shares();
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
  ++seq_;
}

void BookManager::on(const Rpii& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  ++seq_;
}

void BookManager::on(const Dlcr& message) noexcept {
  note_time(message.timestamp_ns());
  check_locate(message.stock_locate());
  ++seq_;
}

void BookManager::check_book(std::uint16_t locate, std::uint64_t timestamp_ns) noexcept {
  const OrderBook& book = books_[locate];
  if (book.touching()) [[unlikely]] {
    classify_touch(book, locate, timestamp_ns);
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
  instruments_.clear();
  state_.reset();
  counters_ = Counters{};
  broken_matches_.clear();
  seq_ = 0;
  last_timestamp_ = 0;
}

}  // namespace itch
