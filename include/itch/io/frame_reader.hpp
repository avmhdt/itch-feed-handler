#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace itch {

/// Iterates the 2-byte big-endian length-prefixed frames of an ITCH file without copying.
/// A frame whose prefix or body runs past the end of the data is reported as Truncated, and
/// offset() stays on the start of that frame (DQ-02).
class FrameReader {
 public:
  enum class Next { Frame, End, Truncated };

  explicit FrameReader(std::span<const std::byte> data) noexcept : data_(data) {}

  Next next(std::span<const std::byte>& out) noexcept {
    const std::size_t remaining = data_.size() - offset_;
    if (remaining == 0) {
      return Next::End;
    }
    if (remaining < kPrefixBytes) {
      return Next::Truncated;
    }
    const auto* prefix = data_.data() + offset_;
    const auto length = static_cast<std::size_t>((std::to_integer<unsigned>(prefix[0]) << 8U) |
                                                 std::to_integer<unsigned>(prefix[1]));
    if (remaining - kPrefixBytes < length) {
      return Next::Truncated;
    }
    out = data_.subspan(offset_ + kPrefixBytes, length);
    offset_ += kPrefixBytes + length;
    return Next::Frame;
  }

  /// Byte offset of the next unread frame.
  [[nodiscard]] std::uint64_t offset() const noexcept { return offset_; }

 private:
  static constexpr std::size_t kPrefixBytes = 2;

  std::span<const std::byte> data_;
  std::size_t offset_ = 0;
};

}  // namespace itch
