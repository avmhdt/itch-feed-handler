#include "itch/book/order_store.hpp"

#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <bit>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace itch {

OpenAddressingMap::OpenAddressingMap(std::size_t capacity_hint) {
  rehash(std::bit_ceil(std::max<std::size_t>(capacity_hint, 8) * 4 / 3 + 1));
}

void OpenAddressingMap::rehash(std::size_t capacity) {
  std::vector<Slot> old = std::move(slots_);
  slots_.assign(capacity, Slot{});
  mask_ = capacity - 1;
  shift_ = 64U - static_cast<unsigned>(std::countr_zero(capacity));
  grow_at_ = capacity / 4 * 3;
  for (const Slot& entry : old) {
    if (entry.value != kNoOrder) {
      std::size_t slot = home(entry.key);
      while (slots_[slot].value != kNoOrder) {
        slot = (slot + 1) & mask_;
      }
      slots_[slot] = entry;
    }
  }
}

bool OpenAddressingMap::insert(std::uint64_t key, std::uint32_t value) {
  if (size_ >= grow_at_) {
    rehash(slots_.size() * 2);
  }
  std::size_t slot = home(key);
  while (slots_[slot].value != kNoOrder) {
    if (slots_[slot].key == key) {
      return false;
    }
    slot = (slot + 1) & mask_;
  }
  slots_[slot] = Slot{key, value};
  ++size_;
  return true;
}

bool OpenAddressingMap::erase(std::uint64_t key) noexcept {
  std::size_t slot = home(key);
  while (slots_[slot].key != key || slots_[slot].value == kNoOrder) {
    if (slots_[slot].value == kNoOrder) {
      return false;
    }
    slot = (slot + 1) & mask_;
  }
  // Backward shift: pull later entries of the probe run into the gap, so that no lookup ever
  // stops early on a hole.
  std::size_t gap = slot;
  std::size_t next = (gap + 1) & mask_;
  while (slots_[next].value != kNoOrder) {
    const std::size_t ideal = home(slots_[next].key);
    if (((next - ideal) & mask_) >= ((next - gap) & mask_)) {
      slots_[gap] = slots_[next];
      gap = next;
    }
    next = (next + 1) & mask_;
  }
  slots_[gap].value = kNoOrder;
  --size_;
  return true;
}

void OpenAddressingMap::clear() noexcept {
  std::fill(slots_.begin(), slots_.end(), Slot{});
  size_ = 0;
}

OrderMapKind parse_order_map(std::string_view name) {
  if (name == "open_addressing") {
    return OrderMapKind::OpenAddressing;
  }
  if (name == "unordered_dense") {
    return OrderMapKind::UnorderedDense;
  }
  if (name == "std") {
    return OrderMapKind::Std;
  }
  throw std::invalid_argument("unknown order map: " + std::string(name));
}

/// The two comparison maps; only one is in use for a given store.
class OrderStore::OtherMap {
 public:
  explicit OtherMap(OrderMapKind kind) : dense_mode_(kind == OrderMapKind::UnorderedDense) {}

  [[nodiscard]] std::uint32_t find(std::uint64_t ref) const noexcept {
    if (dense_mode_) {
      const auto it = dense_.find(ref);
      return it == dense_.end() ? kNoOrder : it->second;
    }
    const auto it = standard_.find(ref);
    return it == standard_.end() ? kNoOrder : it->second;
  }
  bool insert(std::uint64_t ref, std::uint32_t index) {
    return dense_mode_ ? dense_.try_emplace(ref, index).second
                       : standard_.try_emplace(ref, index).second;
  }
  void erase(std::uint64_t ref) noexcept {
    if (dense_mode_) {
      dense_.erase(ref);
    } else {
      standard_.erase(ref);
    }
  }
  void clear() noexcept {
    dense_.clear();
    standard_.clear();
  }

 private:
  ankerl::unordered_dense::map<std::uint64_t, std::uint32_t> dense_;
  std::unordered_map<std::uint64_t, std::uint32_t> standard_;
  bool dense_mode_;
};

OrderStore::OrderStore(OrderMapKind kind, std::size_t capacity_hint)
    : open_(kind == OrderMapKind::OpenAddressing ? capacity_hint : 8), kind_(kind) {
  if (kind != OrderMapKind::OpenAddressing) {
    other_ = std::make_unique<OtherMap>(kind);
  }
  pool_.reserve(capacity_hint);
}

OrderStore::~OrderStore() = default;

std::uint32_t OrderStore::find_other(std::uint64_t ref) const noexcept { return other_->find(ref); }

bool OrderStore::map_insert(std::uint64_t ref, std::uint32_t index) {
  if (kind_ == OrderMapKind::OpenAddressing) [[likely]] {
    return open_.insert(ref, index);
  }
  return other_->insert(ref, index);
}

void OrderStore::map_erase(std::uint64_t ref) noexcept {
  if (kind_ == OrderMapKind::OpenAddressing) [[likely]] {
    open_.erase(ref);
  } else {
    other_->erase(ref);
  }
}

std::uint32_t OrderStore::allocate(std::uint64_t ref) {
  const std::uint32_t index =
      free_head_ != kNoOrder ? free_head_ : static_cast<std::uint32_t>(pool_.size());
  if (!map_insert(ref, index)) {
    return kNoOrder;
  }
  if (free_head_ != kNoOrder) {
    free_head_ = pool_[index].next;
  } else {
    pool_.emplace_back();
  }
  pool_[index].ref = ref;
  ++live_;
  peak_live_ = std::max(peak_live_, live_);
  return index;
}

bool OrderStore::rename(std::uint32_t index, std::uint64_t ref) {
  if (find(ref) != kNoOrder) {
    return false;
  }
  map_erase(pool_[index].ref);
  map_insert(ref, index);
  pool_[index].ref = ref;
  return true;
}

void OrderStore::release(std::uint32_t index) noexcept {
  map_erase(pool_[index].ref);
  pool_[index].next = free_head_;
  free_head_ = index;
  --live_;
}

void OrderStore::clear() noexcept {
  pool_.clear();
  free_head_ = kNoOrder;
  live_ = 0;
  peak_live_ = 0;
  if (kind_ == OrderMapKind::OpenAddressing) {
    open_.clear();
  } else {
    other_->clear();
  }
}

}  // namespace itch
