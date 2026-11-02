#include "itch/proto/endian.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>

namespace {

std::array<std::byte, 32> sequence() {
  std::array<std::byte, 32> bytes{};
  std::array<unsigned char, 32> values{};
  std::iota(values.begin(), values.end(), static_cast<unsigned char>(0xA0));
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::byte>(values[i]);
  }
  return bytes;
}

TEST(Endian, BswapReversesEachWidth) {
  EXPECT_EQ(itch::bswap<std::uint8_t>(0xAB), 0xAB);
  EXPECT_EQ(itch::bswap<std::uint16_t>(0x1234), 0x3412);
  EXPECT_EQ(itch::bswap<std::uint32_t>(0x12345678U), 0x78563412U);
  EXPECT_EQ(itch::bswap<std::uint64_t>(0x0102030405060708ULL), 0x0807060504030201ULL);
}

TEST(Endian, LoadsAreBigEndian) {
  const std::array<std::byte, 8> data{std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
                                      std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
                                      std::byte{0x07}, std::byte{0x08}};
  EXPECT_EQ(itch::load_be<std::uint16_t>(data.data()), 0x0102);
  EXPECT_EQ(itch::load_be<std::uint32_t>(data.data()), 0x01020304U);
  EXPECT_EQ(itch::load_be<std::uint64_t>(data.data()), 0x0102030405060708ULL);
  EXPECT_EQ(itch::load_be48(data.data()), 0x010203040506ULL);
}

TEST(Endian, UnalignedLoadsAgreeWithTheByteAtATimeFallback) {
  const auto data = sequence();
  for (std::size_t offset = 0; offset < 16; ++offset) {
    const std::byte* p = data.data() + offset;
    EXPECT_EQ(itch::load_be<std::uint16_t>(p), itch::load_be_portable<std::uint16_t>(p));
    EXPECT_EQ(itch::load_be<std::uint32_t>(p), itch::load_be_portable<std::uint32_t>(p));
    EXPECT_EQ(itch::load_be<std::uint64_t>(p), itch::load_be_portable<std::uint64_t>(p));
    EXPECT_EQ(itch::load_be48(p), itch::load_be_portable<std::uint64_t>(p) >> 16U);
  }
}

TEST(Endian, ExtremesOfA48BitTimestamp) {
  const std::array<std::byte, 6> all_ones{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                          std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};
  EXPECT_EQ(itch::load_be48(all_ones.data()), 0xFFFFFFFFFFFFULL);
}

TEST(Endian, AsTextKeepsBytes) {
  const std::array<std::byte, 3> data{std::byte{'A'}, std::byte{'B'}, std::byte{' '}};
  EXPECT_EQ(itch::as_text(data.data(), 3), "AB ");
}

}  // namespace
