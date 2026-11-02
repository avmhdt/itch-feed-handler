#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <vector>

namespace itch {

/// Bounded single-producer single-consumer ring. Each side keeps a cached copy of the other's
/// index, so it touches the shared cache line only when the ring looks full or empty.
template <class T>
class SpscQueue {
 public:
  explicit SpscQueue(std::size_t capacity)
      : slots_(std::bit_ceil(capacity < 2 ? std::size_t{2} : capacity)), mask_(slots_.size() - 1) {}

  /// Producer side. Returns false when the ring is full.
  bool try_push(const T& value) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head - cached_tail_ == slots_.size()) {
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (head - cached_tail_ == slots_.size()) {
        return false;
      }
    }
    slots_[head & mask_] = value;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  /// Consumer side. Returns false when the ring is empty.
  bool try_pop(T& out) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    if (tail == cached_head_) {
      cached_head_ = head_.load(std::memory_order_acquire);
      if (tail == cached_head_) {
        return false;
      }
    }
    out = slots_[tail & mask_];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

 private:
  static constexpr std::size_t kCacheLine = 64;

  std::vector<T> slots_;
  std::size_t mask_;
  alignas(kCacheLine) std::atomic<std::size_t> head_{0};
  std::size_t cached_tail_ = 0;
  alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
  std::size_t cached_head_ = 0;
};

}  // namespace itch
