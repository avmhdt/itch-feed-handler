#pragma once

#include <cstdint>

namespace itch {

/// Incremental 64-bit hash of a sequence of integers (FNV-1a over 64-bit words, with a final
/// avalanche). Stable across runs, compilers and machines, so digests can be compared and stored
/// (FR-29).
class Digest {
 public:
  constexpr void add(std::uint64_t value) noexcept {
    state_ = (state_ ^ value) * kPrime;
    state_ ^= state_ >> 29U;
  }

  [[nodiscard]] constexpr std::uint64_t value() const noexcept {
    std::uint64_t mixed = state_;
    mixed ^= mixed >> 33U;
    mixed *= 0xFF51AFD7ED558CCDULL;
    mixed ^= mixed >> 33U;
    return mixed;
  }

 private:
  static constexpr std::uint64_t kPrime = 0x100000001B3ULL;

  std::uint64_t state_ = 0xCBF29CE484222325ULL;
};

}  // namespace itch
