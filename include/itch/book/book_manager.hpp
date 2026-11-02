#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "itch/book/order_book.hpp"
#include "itch/book/order_store.hpp"
#include "itch/check/invariants.hpp"
#include "itch/proto/messages.hpp"
#include "itch/state/instruments.hpp"
#include "itch/state/trading_state.hpp"

namespace itch {

class Sinks;

struct BookConfig {
  OrderMapKind order_map = OrderMapKind::OpenAddressing;
  std::size_t order_capacity_hint = std::size_t{1} << 22;
};

/// Applies every message of the day: dispatches by stock locate to a flat array of books (FR-04),
/// keeps session state and counters, and satisfies MessageHandler.
class BookManager {
 public:
  explicit BookManager(const BookConfig& config = {}, Sinks* sinks = nullptr);

  void on(const SystemEvent& message) noexcept;
  void on(const StockDirectory& message);
  void on(const TradingAction& message) noexcept;
  void on(const RegSho& message) noexcept;
  void on(const ParticipantPosition& message) noexcept;
  void on(const MwcbDecline& message) noexcept;
  void on(const MwcbStatus& message) noexcept;
  void on(const IpoQuoting& message) noexcept;
  void on(const LuldCollar& message) noexcept;
  void on(const OperationalHalt& message) noexcept;
  void on(const AddOrder& message);
  void on(const AddOrderMpid& message);
  void on(const OrderExecuted& message);
  void on(const OrderExecutedPrice& message);
  void on(const OrderCancel& message);
  void on(const OrderDelete& message);
  void on(const OrderReplace& message);
  void on(const Trade& message) noexcept;
  void on(const CrossTrade& message) noexcept;
  void on(const BrokenTrade& message) noexcept;
  void on(const Noii& message) noexcept;
  void on(const Rpii& message) noexcept;
  void on(const Dlcr& message) noexcept;

  [[nodiscard]] const OrderBook* book(std::string_view symbol) const noexcept;
  [[nodiscard]] const OrderBook& book(std::uint16_t locate) const noexcept {
    return books_[locate];
  }
  /// FR-29: hash of every book, ordered by locate, side, price and queue, and of the counters.
  [[nodiscard]] std::uint64_t digest() const noexcept;
  [[nodiscard]] const Counters& counters() const noexcept { return counters_; }
  [[nodiscard]] const SymbolCounters& symbol_counters(std::uint16_t locate) const noexcept {
    return symbol_counters_[locate];
  }
  [[nodiscard]] const LocateTable& instruments() const noexcept { return instruments_; }
  [[nodiscard]] const TradingState& state() const noexcept { return state_; }
  [[nodiscard]] const OrderStore& orders() const noexcept { return store_; }
  /// Messages applied so far; the seq of the next message.
  [[nodiscard]] std::uint64_t messages() const noexcept { return seq_; }
  [[nodiscard]] std::uint64_t last_timestamp() const noexcept { return last_timestamp_; }
  /// Match numbers named by B messages, in arrival order, for the end-of-day correction.
  [[nodiscard]] const std::vector<std::uint64_t>& broken_matches() const noexcept {
    return broken_matches_;
  }
  /// Shares added minus shares removed minus shares resting, for one symbol; 0 under DQ-13.
  [[nodiscard]] std::int64_t accounting_gap(std::uint16_t locate) const noexcept;

  /// Returns to the start of a day, keeping all capacity, so a second pass allocates nothing.
  void reset() noexcept;

 private:
  void note_time(std::uint64_t timestamp_ns) noexcept {
    if (timestamp_ns < last_timestamp_) [[unlikely]] {
      ++counters_.timestamp_regressions;
    }
    last_timestamp_ = timestamp_ns;
  }
  void check_locate(std::uint16_t locate) noexcept {
    if (!instruments_.defined(locate)) [[unlikely]] {
      ++counters_.undefined_locates;
    }
  }
  void add_order(const AddOrder& message, std::uint32_t mpid);
  /// Takes shares off a resting order, clamping per DQ-06, and returns how many came off.
  std::uint32_t take_shares(std::uint32_t index, std::uint32_t shares) noexcept;
  /// After a book change: cheap test for a touching book, slow path only when it does touch.
  void check_book(std::uint16_t locate, std::uint64_t timestamp_ns) noexcept;
  void classify_touch(const OrderBook& book, std::uint16_t locate,
                      std::uint64_t timestamp_ns) noexcept;

  OrderStore store_;
  std::vector<OrderBook> books_;
  std::vector<SymbolCounters> symbol_counters_;
  LocateTable instruments_;
  TradingState state_;
  Counters counters_;
  std::vector<std::uint64_t> broken_matches_;
  std::uint64_t seq_ = 0;
  std::uint64_t last_timestamp_ = 0;
  [[maybe_unused]] Sinks* sinks_;
};

}  // namespace itch
