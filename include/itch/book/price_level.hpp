#pragma once

#include <cstdint>

namespace itch {

enum class Side : std::int8_t { Bid = 1, Ask = -1 };

/// A price level as seen from outside the book.
struct Level {
  std::int64_t price_ticks;
  std::int64_t qty;
  std::int32_t orders;

  friend bool operator==(const Level&, const Level&) = default;
};

/// A price level inside the book: aggregate size plus the two ends of its FIFO queue, held as
/// indices into the order pool.
struct PriceLevel {
  std::int64_t qty = 0;
  std::uint32_t price = 0;
  std::int32_t orders = 0;
  std::uint32_t head = 0;
  std::uint32_t tail = 0;
};

}  // namespace itch
