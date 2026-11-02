#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <version>

namespace itch {

/// Reverses the bytes of an unsigned integer. std::byteswap is C++23, so it is used only when the
/// standard library provides it.
template <std::unsigned_integral T>
[[nodiscard]] constexpr T bswap(T v) noexcept {
#if defined(__cpp_lib_byteswap)
  return std::byteswap(v);
#else
  if constexpr (sizeof(T) == 1) {
    return v;
  } else if constexpr (sizeof(T) == 2) {
    return __builtin_bswap16(v);
  } else if constexpr (sizeof(T) == 4) {
    return __builtin_bswap32(v);
  } else {
    static_assert(sizeof(T) == 8);
    return __builtin_bswap64(v);
  }
#endif
}

/// Byte-at-a-time big-endian load. Correct on any host; also the reference for load_be in tests
/// (NFR-11).
template <std::unsigned_integral T>
[[nodiscard]] constexpr T load_be_portable(const std::byte* p) noexcept {
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value = static_cast<T>((static_cast<std::uint64_t>(value) << 8U) | std::to_integer<T>(p[i]));
  }
  return value;
}

/// Big-endian load from a possibly unaligned address. ITCH fields sit at arbitrary offsets, so
/// this goes through std::memcpy rather than a pointer cast (ITCH 5.0 "Data Types").
template <std::unsigned_integral T>
[[nodiscard]] inline T load_be(const std::byte* p) noexcept {
  if constexpr (std::endian::native == std::endian::little) {
    T value;
    std::memcpy(&value, p, sizeof(T));
    return bswap(value);
  } else {
    return load_be_portable<T>(p);
  }
}

/// Loads a 48-bit big-endian integer, the width of every ITCH timestamp.
[[nodiscard]] inline std::uint64_t load_be48(const std::byte* p) noexcept {
  const auto high = static_cast<std::uint64_t>(load_be<std::uint16_t>(p));
  const auto low = static_cast<std::uint64_t>(load_be<std::uint32_t>(p + 2));
  return (high << 32U) | low;
}

/// Views bytes as text. Character types may alias anything, so this is the one permitted cast in
/// the decoder.
[[nodiscard]] inline std::string_view as_text(const std::byte* p, std::size_t n) noexcept {
  return {reinterpret_cast<const char*>(p), n};  // NOLINT(*-pro-type-reinterpret-cast)
}

}  // namespace itch
