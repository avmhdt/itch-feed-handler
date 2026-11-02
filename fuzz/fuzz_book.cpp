// FR-27: bytes read as a structured operation sequence, applied to the order book and to a small
// reference model. Any disagreement aborts, which libFuzzer reports as a crash.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "itch/book/order_book.hpp"

namespace {

using itch::Level;
using itch::OrderBook;
using itch::OrderStore;
using itch::Side;

struct Resting {
  std::uint64_t ref;
  std::int64_t shares;
};

class Model {
 public:
  void add(std::uint64_t ref, Side side, std::uint32_t price, std::int64_t shares) {
    levels(side)[price].push_back({ref, shares});
    index_[ref] = {side, price};
  }
  [[nodiscard]] bool live(std::uint64_t ref) const { return index_.contains(ref); }
  void reduce(std::uint64_t ref, std::int64_t shares) {
    const auto [side, price] = index_.at(ref);
    auto& queue = levels(side).at(price);
    const auto it =
        std::find_if(queue.begin(), queue.end(), [ref](const Resting& r) { return r.ref == ref; });
    it->shares -= std::min(shares, it->shares);
    if (it->shares == 0) {
      queue.erase(it);
      if (queue.empty()) {
        levels(side).erase(price);
      }
      index_.erase(ref);
    }
  }
  void remove(std::uint64_t ref) { reduce(ref, std::numeric_limits<std::int64_t>::max()); }
  void replace(std::uint64_t old_ref, std::uint64_t new_ref, std::uint32_t price,
               std::int64_t shares) {
    const Side side = index_.at(old_ref).first;
    remove(old_ref);
    add(new_ref, side, price, shares);
  }
  [[nodiscard]] std::vector<Level> depth(Side side) {
    std::vector<Level> out;
    const auto append = [&out](const auto& entry) {
      std::int64_t qty = 0;
      for (const Resting& r : entry.second) {
        qty += r.shares;
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

 private:
  using Levels = std::map<std::uint32_t, std::list<Resting>>;
  Levels& levels(Side side) { return side == Side::Bid ? bids_ : asks_; }

  Levels bids_;
  Levels asks_;
  std::unordered_map<std::uint64_t, std::pair<Side, std::uint32_t>> index_;
};

void require(bool condition) {
  if (!condition) {
    std::abort();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  constexpr std::size_t kRecord = 4;
  OrderStore store(itch::OrderMapKind::OpenAddressing, 16);
  OrderBook book(store);
  Model model;
  std::array<Level, 64> levels{};

  for (std::size_t i = 0; i + kRecord <= size && i < 4096 * kRecord; i += kRecord) {
    const std::uint8_t op = data[i] & 7U;
    const std::uint64_t ref = data[i + 1] & 0x3FU;
    const std::uint32_t price = 1U + (data[i + 2] & 0x1FU);
    const std::int64_t shares = 1 + data[i + 3];
    const Side side = (data[i] & 8U) != 0 ? Side::Bid : Side::Ask;
    const std::uint64_t new_ref = ref ^ (static_cast<std::uint64_t>(data[i + 2]) << 6U);

    if (op <= 2) {
      if (!model.live(ref)) {
        book.add(ref, side, price, shares);
        model.add(ref, side, price, shares);
      }
    } else if (op <= 4) {
      if (model.live(ref)) {
        book.execute(ref, shares);
        model.reduce(ref, shares);
      } else {
        require(book.execute(ref, shares) == -1);
      }
    } else if (op == 5) {
      if (model.live(ref)) {
        model.remove(ref);
      }
      book.remove(ref);
    } else if (model.live(ref) && !model.live(new_ref)) {
      book.replace(ref, new_ref, price, shares);
      model.replace(ref, new_ref, price, shares);
    }

    for (const Side checked : {Side::Bid, Side::Ask}) {
      const std::size_t count = book.levels(checked, levels);
      const std::vector<Level> expected = model.depth(checked);
      require(count == std::min(expected.size(), levels.size()));
      require(std::equal(levels.begin(), levels.begin() + static_cast<std::ptrdiff_t>(count),
                         expected.begin()));
    }
  }
  return 0;
}
