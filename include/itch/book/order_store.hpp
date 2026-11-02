#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <vector>

#include "itch/book/price_level.hpp"

namespace itch {

inline constexpr std::uint32_t kNoOrder = std::numeric_limits<std::uint32_t>::max();

/// One live order. Queue links are pool indices, so the pool can grow without fixing up pointers
/// and a node stays small enough for two to share a cache line.
struct OrderNode {
  std::uint64_t ref = 0;
  std::uint32_t price = 0;
  std::uint32_t shares = 0;
  std::uint32_t prev = kNoOrder;
  std::uint32_t next = kNoOrder;
  std::uint32_t mpid = 0;  // The four attribution bytes as loaded; 0 for none.
  std::uint16_t locate = 0;
  Side side = Side::Bid;
};
static_assert(sizeof(OrderNode) == 32);

/// Linear-probing table from order reference to pool index, with backward-shift deletion so that
/// heavy add and delete traffic leaves no tombstones behind. References are close to sequential,
/// so they go through a Fibonacci multiply before they index the table.
class OpenAddressingMap {
 public:
  explicit OpenAddressingMap(std::size_t capacity_hint);

  [[nodiscard]] std::uint32_t find(std::uint64_t key) const noexcept {
    std::size_t slot = home(key);
    while (slots_[slot].value != kNoOrder) {
      if (slots_[slot].key == key) {
        return slots_[slot].value;
      }
      slot = (slot + 1) & mask_;
    }
    return kNoOrder;
  }

  /// Inserts a new key, or returns false and changes nothing when the key already exists.
  bool insert(std::uint64_t key, std::uint32_t value);
  bool erase(std::uint64_t key) noexcept;
  void clear() noexcept;

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

 private:
  struct Slot {
    std::uint64_t key = 0;
    std::uint32_t value = kNoOrder;
  };

  static constexpr std::uint64_t kFibonacci = 0x9E3779B97F4A7C15ULL;

  [[nodiscard]] std::size_t home(std::uint64_t key) const noexcept {
    return static_cast<std::size_t>((key * kFibonacci) >> shift_);
  }
  void rehash(std::size_t capacity);

  std::vector<Slot> slots_;
  std::size_t mask_ = 0;
  std::size_t size_ = 0;
  std::size_t grow_at_ = 0;
  unsigned shift_ = 0;
};

enum class OrderMapKind { OpenAddressing, UnorderedDense, Std };

/// Parses "open_addressing", "unordered_dense" or "std"; throws std::invalid_argument otherwise.
[[nodiscard]] OrderMapKind parse_order_map(std::string_view name);

/// All live orders of the day, shared by every book: references are unique across symbols
/// (ITCH 5.0 1.3), so one table serves them all. The reference map is chosen at run time for the
/// comparison in AC-H-05; the default is reached through an inlined branch rather than a virtual
/// call (NFR-12).
class OrderStore {
 public:
  explicit OrderStore(OrderMapKind kind = OrderMapKind::OpenAddressing,
                      std::size_t capacity_hint = std::size_t{1} << 16);
  ~OrderStore();

  OrderStore(const OrderStore&) = delete;
  OrderStore& operator=(const OrderStore&) = delete;

  [[nodiscard]] std::uint32_t find(std::uint64_t ref) const noexcept {
    if (kind_ == OrderMapKind::OpenAddressing) [[likely]] {
      return open_.find(ref);
    }
    return find_other(ref);
  }

  /// Takes a node for a new reference; kNoOrder if that reference is already live.
  std::uint32_t allocate(std::uint64_t ref);
  /// Gives a node a new reference, keeping everything else; false if `ref` is already live.
  bool rename(std::uint32_t index, std::uint64_t ref);
  void release(std::uint32_t index) noexcept;
  /// Forgets every order but keeps the memory, so a second pass allocates nothing.
  void clear() noexcept;

  [[nodiscard]] OrderNode& operator[](std::uint32_t index) noexcept { return pool_[index]; }
  [[nodiscard]] const OrderNode& operator[](std::uint32_t index) const noexcept {
    return pool_[index];
  }
  [[nodiscard]] std::size_t live() const noexcept { return live_; }
  [[nodiscard]] std::size_t peak_live() const noexcept { return peak_live_; }

 private:
  class OtherMap;

  [[nodiscard]] std::uint32_t find_other(std::uint64_t ref) const noexcept;
  bool map_insert(std::uint64_t ref, std::uint32_t index);
  void map_erase(std::uint64_t ref) noexcept;

  std::vector<OrderNode> pool_;
  OpenAddressingMap open_;
  std::unique_ptr<OtherMap> other_;
  OrderMapKind kind_;
  std::uint32_t free_head_ = kNoOrder;
  std::size_t live_ = 0;
  std::size_t peak_live_ = 0;
};

}  // namespace itch
