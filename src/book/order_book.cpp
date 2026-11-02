#include "itch/book/order_book.hpp"

#include <algorithm>

namespace itch {

namespace {

constexpr std::size_t kNearTouch = 8;

/// True when level price `a` sits further from the touch than `b`, the sort order of a side.
constexpr bool before(Side s, std::uint32_t a, std::uint32_t b) noexcept {
  return s == Side::Bid ? a < b : a > b;
}

constexpr Level to_level(const PriceLevel& level) noexcept {
  return {static_cast<std::int64_t>(level.price), level.qty, level.orders};
}

}  // namespace

std::size_t OrderBook::position(const std::vector<PriceLevel>& levels, Side s,
                                std::uint32_t price) noexcept {
  // Scan the few levels nearest the touch first, since nearly every message lands there.
  std::size_t i = levels.size();
  const std::size_t stop = i > kNearTouch ? i - kNearTouch : 0;
  while (i > stop) {
    if (!before(s, price, levels[i - 1].price)) {
      return levels[i - 1].price == price ? i - 1 : i;
    }
    --i;
  }
  if (i == 0) {
    return 0;
  }
  const auto begin = levels.begin();
  const auto end = begin + static_cast<std::ptrdiff_t>(i);
  const auto it = std::lower_bound(
      begin, end, price,
      [s](const PriceLevel& level, std::uint32_t p) { return before(s, level.price, p); });
  return static_cast<std::size_t>(it - begin);
}

void OrderBook::link(std::uint32_t index) {
  OrderStore& store = *store_;
  OrderNode& node = store[index];
  std::vector<PriceLevel>& levels = side(node.side);
  const std::size_t at = position(levels, node.side, node.price);
  if (at == levels.size() || levels[at].price != node.price) {
    PriceLevel level;
    level.price = node.price;
    level.head = index;
    level.tail = index;
    level.qty = node.shares;
    level.orders = 1;
    levels.insert(levels.begin() + static_cast<std::ptrdiff_t>(at), level);
    node.prev = kNoOrder;
    node.next = kNoOrder;
    return;
  }
  PriceLevel& level = levels[at];
  node.prev = level.tail;
  node.next = kNoOrder;
  store[level.tail].next = index;
  level.tail = index;
  level.qty += node.shares;
  ++level.orders;
}

void OrderBook::unlink(std::uint32_t index) noexcept {
  OrderStore& store = *store_;
  const OrderNode& node = store[index];
  std::vector<PriceLevel>& levels = side(node.side);
  const std::size_t at = position(levels, node.side, node.price);
  PriceLevel& level = levels[at];
  if (node.prev != kNoOrder) {
    store[node.prev].next = node.next;
  } else {
    level.head = node.next;
  }
  if (node.next != kNoOrder) {
    store[node.next].prev = node.prev;
  } else {
    level.tail = node.prev;
  }
  level.qty -= node.shares;
  if (--level.orders == 0) {
    levels.erase(levels.begin() + static_cast<std::ptrdiff_t>(at));
  }
}

void OrderBook::reduce(std::uint32_t index, std::uint32_t shares) noexcept {
  OrderNode& node = (*store_)[index];
  if (node.shares == shares) {
    unlink(index);
    store_->release(index);
    return;
  }
  std::vector<PriceLevel>& levels = side(node.side);
  levels[position(levels, node.side, node.price)].qty -= shares;
  node.shares -= shares;
}

void OrderBook::relink(std::uint32_t index, std::uint32_t price, std::uint32_t shares) {
  unlink(index);
  OrderNode& node = (*store_)[index];
  node.price = price;
  node.shares = shares;
  link(index);
}

void OrderBook::add(std::uint64_t ref, Side s, std::int64_t px, std::int64_t qty) {
  const std::uint32_t index = store_->allocate(ref);
  if (index == kNoOrder) {
    return;
  }
  OrderNode& node = (*store_)[index];
  node.side = s;
  node.price = static_cast<std::uint32_t>(px);
  node.shares = static_cast<std::uint32_t>(qty);
  node.mpid = 0;
  link(index);
}

std::int64_t OrderBook::execute(std::uint64_t ref, std::int64_t qty) {
  const std::uint32_t index = store_->find(ref);
  if (index == kNoOrder) {
    return -1;
  }
  const std::int64_t have = (*store_)[index].shares;
  const std::int64_t taken = std::min(qty, have);
  reduce(index, static_cast<std::uint32_t>(taken));
  return have - taken;
}

std::int64_t OrderBook::cancel(std::uint64_t ref, std::int64_t qty) { return execute(ref, qty); }

void OrderBook::remove(std::uint64_t ref) {
  const std::uint32_t index = store_->find(ref);
  if (index != kNoOrder) {
    unlink(index);
    store_->release(index);
  }
}

void OrderBook::replace(std::uint64_t old_ref, std::uint64_t new_ref, std::int64_t px,
                        std::int64_t qty) {
  const std::uint32_t index = store_->find(old_ref);
  if (index != kNoOrder && store_->rename(index, new_ref)) {
    relink(index, static_cast<std::uint32_t>(px), static_cast<std::uint32_t>(qty));
  }
}

std::optional<Level> OrderBook::best(Side s) const noexcept {
  const std::vector<PriceLevel>& levels = side(s);
  if (levels.empty()) {
    return std::nullopt;
  }
  return to_level(levels.back());
}

std::size_t OrderBook::levels(Side s, std::span<Level> out) const noexcept {
  const std::vector<PriceLevel>& source = side(s);
  const std::size_t count = std::min(out.size(), source.size());
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = to_level(source[source.size() - 1 - i]);
  }
  return count;
}

bool OrderBook::crossed() const noexcept {
  return !bids_.empty() && !asks_.empty() && bids_.back().price > asks_.back().price;
}

bool OrderBook::locked() const noexcept {
  return !bids_.empty() && !asks_.empty() && bids_.back().price == asks_.back().price;
}

void OrderBook::reserve(std::size_t levels) {
  bids_.reserve(levels);
  asks_.reserve(levels);
}

void OrderBook::clear() noexcept {
  bids_.clear();
  asks_.clear();
}

}  // namespace itch
