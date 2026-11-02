#include "itch/io/frame_reader.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
  std::vector<std::byte> out;
  for (const unsigned v : values) {
    out.push_back(static_cast<std::byte>(v));
  }
  return out;
}

TEST(FrameReader, EmptyInputEndsImmediately) {
  itch::FrameReader reader({});
  std::span<const std::byte> frame;
  EXPECT_EQ(reader.next(frame), itch::FrameReader::Next::End);
  EXPECT_EQ(reader.offset(), 0U);
}

TEST(FrameReader, YieldsFramesInOrderWithoutCopying) {
  const auto data = bytes({0, 2, 'S', 1, 0, 1, 'H'});
  itch::FrameReader reader(data);
  std::span<const std::byte> frame;

  ASSERT_EQ(reader.next(frame), itch::FrameReader::Next::Frame);
  EXPECT_EQ(frame.data(), data.data() + 2);
  EXPECT_EQ(frame.size(), 2U);
  EXPECT_EQ(reader.offset(), 4U);

  ASSERT_EQ(reader.next(frame), itch::FrameReader::Next::Frame);
  EXPECT_EQ(frame.size(), 1U);
  EXPECT_EQ(std::to_integer<char>(frame[0]), 'H');

  EXPECT_EQ(reader.next(frame), itch::FrameReader::Next::End);
}

TEST(FrameReader, ZeroLengthFrameIsAFrame) {
  const auto data = bytes({0, 0, 0, 1, 'S'});
  itch::FrameReader reader(data);
  std::span<const std::byte> frame;
  ASSERT_EQ(reader.next(frame), itch::FrameReader::Next::Frame);
  EXPECT_TRUE(frame.empty());
  ASSERT_EQ(reader.next(frame), itch::FrameReader::Next::Frame);
  EXPECT_EQ(frame.size(), 1U);
}

TEST(FrameReader, LoneLengthByteIsTruncated) {
  const auto data = bytes({0, 1, 'S', 0});
  itch::FrameReader reader(data);
  std::span<const std::byte> frame;
  ASSERT_EQ(reader.next(frame), itch::FrameReader::Next::Frame);
  EXPECT_EQ(reader.next(frame), itch::FrameReader::Next::Truncated);
  EXPECT_EQ(reader.offset(), 3U);
}

TEST(FrameReader, BodyPastEndIsTruncatedAndOffsetStaysOnTheFrame) {
  const auto data = bytes({0, 5, 'S', 1});
  itch::FrameReader reader(data);
  std::span<const std::byte> frame;
  EXPECT_EQ(reader.next(frame), itch::FrameReader::Next::Truncated);
  EXPECT_EQ(reader.offset(), 0U);
  EXPECT_EQ(reader.next(frame), itch::FrameReader::Next::Truncated);
}

TEST(FrameReader, MaximumLengthFrameIsAccepted) {
  std::vector<std::byte> data(2 + 65535, std::byte{0x41});
  data[0] = std::byte{0xFF};
  data[1] = std::byte{0xFF};
  itch::FrameReader reader(data);
  std::span<const std::byte> frame;
  ASSERT_EQ(reader.next(frame), itch::FrameReader::Next::Frame);
  EXPECT_EQ(frame.size(), 65535U);
  EXPECT_EQ(reader.next(frame), itch::FrameReader::Next::End);
}

}  // namespace
