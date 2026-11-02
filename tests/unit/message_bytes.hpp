#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace itch::test {

/// Builds a message field by field, in the order of the ITCH 5.0 field tables, so a test reads
/// like the table it comes from.
class MessageBytes {
 public:
  /// Starts with the common header: type, locate, tracking number, 48-bit timestamp.
  MessageBytes(char type, std::uint16_t locate, std::uint16_t tracking, std::uint64_t timestamp) {
    alpha(type).u16(locate).u16(tracking).u48(timestamp);
  }

  MessageBytes& alpha(char value) {
    bytes_.push_back(static_cast<std::byte>(value));
    return *this;
  }
  MessageBytes& text(std::string_view value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
      alpha(i < value.size() ? value[i] : ' ');
    }
    return *this;
  }
  MessageBytes& u16(std::uint16_t value) { return big_endian(value, 2); }
  MessageBytes& u32(std::uint32_t value) { return big_endian(value, 4); }
  MessageBytes& u48(std::uint64_t value) { return big_endian(value, 6); }
  MessageBytes& u64(std::uint64_t value) { return big_endian(value, 8); }

  [[nodiscard]] const std::vector<std::byte>& bytes() const noexcept { return bytes_; }

 private:
  MessageBytes& big_endian(std::uint64_t value, std::size_t width) {
    for (std::size_t i = width; i > 0; --i) {
      bytes_.push_back(static_cast<std::byte>((value >> ((i - 1) * 8U)) & 0xFFU));
    }
    return *this;
  }

  std::vector<std::byte> bytes_;
};

}  // namespace itch::test
