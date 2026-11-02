// Random valid operation sequences against a deliberately simple std::map book: identical L2 and
// L3 state after every operation, the accounting identity, and equal results across order maps.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "itch/book/order_book.hpp"

namespace {

using itch::Level;
using itch::OrderBook;
using itch::OrderMapKind;
using itch::OrderStore;
using itch::Side;

struct Op {
  int kind;            // 0 add, 1 execute, 2 cancel, 3 delete, 4 replace
  std::uint32_t pick;  // Chooses the target among live orders.
  std::uint32_t price;
  std::uint32_t shares;
  bool bid;
};

struct RefOrder {
  std::uint64_t ref;
  std::uint32_t shares;
};

/// The slow model: price to FIFO list per side, and a linear scan to find an order.
class ReferenceBook {
 public:
  void add(std::uint64_t ref, Side side, std::uint32_t price, std::uint32_t shares) {
    levels(side)[price].push_back({ref, shares});
    sides_[ref] = {side, price};
    added_ += shares;
  }

  std::int64_t reduce(std::uint64_t ref, std::uint32_t shares) {
    const auto [side, price] = sides_.at(ref);
    auto& queue = levels(side).at(price);
    auto it =
        std::find_if(queue.begin(), queue.end(), [ref](const RefOrder& o) { return o.ref == ref; });
    const std::uint32_t taken = std::min(shares, it->shares);
    it->shares -= taken;
    removed_ += taken;
    const std::int64_t left = it->shares;
    if (left == 0) {
      queue.erase(it);
      if (queue.empty()) {
        levels(side).erase(price);
      }
      sides_.erase(ref);
    }
    return left;
  }

  void remove(std::uint64_t ref) {
    const auto [side, price] = sides_.at(ref);
    const auto& queue = levels(side).at(price);
    const auto it =
        std::find_if(queue.begin(), queue.end(), [ref](const RefOrder& o) { return o.ref == ref; });
    reduce(ref, it->shares);
  }

  void replace(std::uint64_t old_ref, std::uint64_t new_ref, std::uint32_t price,
               std::uint32_t shares) {
    const Side side = sides_.at(old_ref).first;
    remove(old_ref);
    add(new_ref, side, price, shares);
  }

  [[nodiscard]] std::vector<Level> depth(Side side) const {
    std::vector<Level> out;
    const auto append = [&out](const auto& entry) {
      std::int64_t qty = 0;
      for (const RefOrder& order : entry.second) {
        qty += order.shares;
      }
      out.push_back({entry.first, qty, static_cast<std::int32_t>(entry.second.size())});
    };
    if (side == Side::Bid) {
      std::for_each(bids_.rbegin(), bids_.rend(), append);
    } else {
      std::for_each(asks_.begin(), asks_.end(), append);
    }
    return out;
  }

  [[nodiscard]] std::vector<std::pair<std::uint64_t, std::uint32_t>> queue(Side side) const {
    std::vector<std::pair<std::uint64_t, std::uint32_t>> out;
    const auto append = [&out](const auto& entry) {
      for (const RefOrder& order : entry.second) {
        out.emplace_back(order.ref, order.shares);
      }
    };
    if (side == Side::Bid) {
      std::for_each(bids_.rbegin(), bids_.rend(), append);
    } else {
      std::for_each(asks_.begin(), asks_.end(), append);
    }
    return out;
  }

  [[nodiscard]] std::vector<std::uint64_t> live() const {
    std::vector<std::uint64_t> refs;
    for (const auto& entry : sides_) {
      refs.push_back(entry.first);
    }
    std::sort(refs.begin(), refs.end());
    return refs;
  }

  [[nodiscard]] std::int64_t added() const { return added_; }
  [[nodiscard]] std::int64_t removed() const { return removed_; }

 private:
  using Levels = std::map<std::uint32_t, std::list<RefOrder>>;

  Levels& levels(Side side) { return side == Side::Bid ? bids_ : asks_; }

  Levels bids_;
  Levels asks_;
  std::unordered_map<std::uint64_t, std::pair<Side, std::uint32_t>> sides_;
  std::int64_t added_ = 0;
  std::int64_t removed_ = 0;
};

std::vector<Level> real_depth(const OrderBook& book, Side side) {
  std::vector<Level> out(512);
  out.resize(book.levels(side, out));
  return out;
}

std::vector<std::pair<std::uint64_t, std::uint32_t>> real_queue(const OrderBook& book, Side side) {
  std::vector<std::pair<std::uint64_t, std::uint32_t>> out;
  book.for_each_order(
      side, [&out](const itch::OrderNode& node) { out.emplace_back(node.ref, node.shares); });
  return out;
}

rc::Gen<Op> op_gen() {
  return rc::gen::build<Op>(rc::gen::set(&Op::kind, rc::gen::inRange(0, 5)),
                            rc::gen::set(&Op::pick, rc::gen::arbitrary<std::uint32_t>()),
                            rc::gen::set(&Op::price, rc::gen::inRange<std::uint32_t>(1, 40)),
                            rc::gen::set(&Op::shares, rc::gen::inRange<std::uint32_t>(1, 500)),
                            rc::gen::set(&Op::bid, rc::gen::arbitrary<bool>()));
}

/// Applies the operations to a book and the model, checking them against each other throughout.
/// Returns a summary of the final state, to compare across order maps.
std::vector<std::uint64_t> run_ops(const std::vector<Op>& ops, OrderMapKind kind) {
  OrderStore store(kind, 16);
  OrderBook book(store);
  ReferenceBook reference;
  std::uint64_t next_ref = 1;

  for (const Op& op : ops) {
    const auto live = reference.live();
    if (op.kind == 0 || live.empty()) {
      const std::uint64_t ref = next_ref++;
      const Side side = op.bid ? Side::Bid : Side::Ask;
      book.add(ref, side, op.price, op.shares);
      reference.add(ref, side, op.price, op.shares);
    } else {
      const std::uint64_t ref = live[op.pick % live.size()];
      if (op.kind == 1 || op.kind == 2) {
        const std::int64_t left = book.execute(ref, op.shares);
        RC_ASSERT(left == reference.reduce(ref, op.shares));
      } else if (op.kind == 3) {
        book.remove(ref);
        reference.remove(ref);
      } else {
        const std::uint64_t new_ref = next_ref++;
        book.replace(ref, new_ref, op.price, op.shares);
        reference.replace(ref, new_ref, op.price, op.shares);
      }
    }
    RC_ASSERT(real_depth(book, Side::Bid) == reference.depth(Side::Bid));
    RC_ASSERT(real_depth(book, Side::Ask) == reference.depth(Side::Ask));
    RC_ASSERT(real_queue(book, Side::Bid) == reference.queue(Side::Bid));
    RC_ASSERT(real_queue(book, Side::Ask) == reference.queue(Side::Ask));
  }

  std::int64_t resting = 0;
  std::vector<std::uint64_t> summary;
  for (const Side side : {Side::Bid, Side::Ask}) {
    for (const auto& [ref, shares] : real_queue(book, side)) {
      resting += shares;
      summary.push_back(ref);
      summary.push_back(shares);
    }
  }
  RC_ASSERT(reference.added() == reference.removed() + resting);
  return summary;
}

RC_GTEST_PROP(BookProperty, MatchesTheSlowReferenceBook, ()) {
  const auto ops = *rc::gen::container<std::vector<Op>>(op_gen());
  run_ops(ops, OrderMapKind::OpenAddressing);
}

RC_GTEST_PROP(BookProperty, OrderMapsAreInterchangeable, ()) {
  const auto ops = *rc::gen::container<std::vector<Op>>(op_gen());
  const auto open = run_ops(ops, OrderMapKind::OpenAddressing);
  RC_ASSERT(run_ops(ops, OrderMapKind::UnorderedDense) == open);
  RC_ASSERT(run_ops(ops, OrderMapKind::Std) == open);
}

}  // namespace
