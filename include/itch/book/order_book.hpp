#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include "itch/book/order_store.hpp"
#include "itch/book/price_level.hpp"

namespace itch {

/// Price-time book of one symbol over a shared OrderStore. Each side keeps its levels in a vector
/// sorted so the best price is at the back: nearly all activity is near the touch, so insertions
/// and removals move very little. Each level is a FIFO queue threaded through the order pool.
class OrderBook {
 public:
  explicit OrderBook(OrderStore& store) : store_(&store) {}

  /// Queue position is lost by adding at the tail (FR-05). A duplicate reference is ignored.
  void add(std::uint64_t ref, Side s, std::int64_t px, std::int64_t qty);
  /// Removes `qty` shares and returns what remains, or -1 for an unknown reference.
  std::int64_t execute(std::uint64_t ref, std::int64_t qty);
  std::int64_t cancel(std::uint64_t ref, std::int64_t qty);
  void remove(std::uint64_t ref);
  /// ITCH 5.0 1.4.5: the old reference leaves the book and the new one joins the back of the queue
  /// at its price, keeping side and attribution. Priority is lost even at an unchanged price.
  void replace(std::uint64_t old_ref, std::uint64_t new_ref, std::int64_t px, std::int64_t qty);

  [[nodiscard]] std::optional<Level> best(Side s) const noexcept;
  /// Copies up to out.size() levels, best first, and returns how many.
  std::size_t levels(Side s, std::span<Level> out) const noexcept;
  [[nodiscard]] std::size_t level_count(Side s) const noexcept { return side(s).size(); }
  [[nodiscard]] bool crossed() const noexcept;
  [[nodiscard]] bool locked() const noexcept;
  /// Crossed or locked: the one test that runs after every book change.
  [[nodiscard]] bool touching() const noexcept {
    return !bids_.empty() && !asks_.empty() && bids_.back().price >= asks_.back().price;
  }

  /// Pool-index forms for BookManager, which has already resolved the reference.
  void link(std::uint32_t index);
  void unlink(std::uint32_t index) noexcept;
  /// Removes shares from a linked order; the caller guarantees `shares` is at most what is left.
  void reduce(std::uint32_t index, std::uint32_t shares) noexcept;
  /// Moves a linked order to the back of the queue at a new price and size.
  void relink(std::uint32_t index, std::uint32_t price, std::uint32_t shares);

  /// Calls `visit(const OrderNode&)` for each order on a side, best level first, in queue order.
  template <class Visit>
  void for_each_order(Side s, Visit&& visit) const {
    for (const PriceLevel& level : std::views::reverse(side(s))) {
      for (std::uint32_t i = level.head; i != kNoOrder; i = (*store_)[i].next) {
        visit((*store_)[i]);
      }
    }
  }
  /// Calls `visit(const PriceLevel&)` for each level on a side, best first.
  template <class Visit>
  void for_each_level(Side s, Visit&& visit) const {
    for (const PriceLevel& level : std::views::reverse(side(s))) {
      visit(level);
    }
  }

  /// Gives both sides room for `levels` prices, so later growth is rare.
  void reserve(std::size_t levels);
  /// Empties the book, keeping its capacity.
  void clear() noexcept;

 private:
  [[nodiscard]] std::vector<PriceLevel>& side(Side s) noexcept {
    return s == Side::Bid ? bids_ : asks_;
  }
  [[nodiscard]] const std::vector<PriceLevel>& side(Side s) const noexcept {
    return s == Side::Bid ? bids_ : asks_;
  }
  /// Position of `price` in a side, or where it would be inserted.
  [[nodiscard]] static std::size_t position(const std::vector<PriceLevel>& levels, Side s,
                                            std::uint32_t price) noexcept;

  OrderStore* store_;
  std::vector<PriceLevel> bids_;  // Ascending price: the best bid is at the back.
  std::vector<PriceLevel> asks_;  // Descending price: the best ask is at the back.
};

}  // namespace itch
