#include "itch/proto/parse.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

#include "itch/io/file_driver.hpp"
#include "message_bytes.hpp"

namespace {

using itch::MsgType;
using itch::ParseStatus;

/// Records the type each on() was called with.
struct Recorder {
  std::vector<MsgType> seen;

  template <class View>
  void on(const View& message) {
    EXPECT_EQ(message.type(), View::kType);
    seen.push_back(View::kType);
  }
};

static_assert(itch::MessageHandler<Recorder>);

std::vector<std::byte> blank(MsgType type, std::size_t length) {
  std::vector<std::byte> bytes(length);
  bytes[0] = static_cast<std::byte>(type);
  return bytes;
}

TEST(SpecTable, HasEveryTypeWithItsLength) {
  EXPECT_EQ(itch::kTypeLengths.size(), 23U);
  EXPECT_EQ(itch::message_length(MsgType::AddOrder), 36U);
  EXPECT_EQ(itch::message_length(MsgType::OperationalHalt), 21U);
  EXPECT_EQ(itch::message_length(static_cast<MsgType>('z')), 0U);
}

TEST(Parse, DispatchesEveryTypeOnce) {
  Recorder recorder;
  for (const itch::TypeLength& entry : itch::kTypeLengths) {
    const auto message = blank(entry.type, entry.length);
    EXPECT_EQ(itch::parse(message, recorder), ParseStatus::Ok);
  }
  ASSERT_EQ(recorder.seen.size(), itch::kTypeLengths.size());
  for (std::size_t i = 0; i < recorder.seen.size(); ++i) {
    EXPECT_EQ(recorder.seen[i], itch::kTypeLengths[i].type);
  }
}

TEST(Parse, EmptyFrameIsBadLength) {
  Recorder recorder;
  EXPECT_EQ(itch::parse({}, recorder), ParseStatus::BadLength);
  EXPECT_TRUE(recorder.seen.empty());
}

TEST(Parse, UnknownTypeReachesNoHandler) {
  Recorder recorder;
  const auto message = blank(static_cast<MsgType>('z'), 12);
  EXPECT_EQ(itch::parse(message, recorder), ParseStatus::UnknownType);
  EXPECT_TRUE(recorder.seen.empty());
}

TEST(Parse, WrongLengthReachesNoHandler) {
  Recorder recorder;
  for (const itch::TypeLength& entry : itch::kTypeLengths) {
    const auto short_message = blank(entry.type, entry.length - 1U);
    const auto long_message = blank(entry.type, entry.length + 1U);
    EXPECT_EQ(itch::parse(short_message, recorder), ParseStatus::BadLength);
    EXPECT_EQ(itch::parse(long_message, recorder), ParseStatus::BadLength);
  }
  EXPECT_TRUE(recorder.seen.empty());
}

std::vector<std::byte> framed(std::initializer_list<std::vector<std::byte>> messages) {
  std::vector<std::byte> out;
  for (const auto& message : messages) {
    out.push_back(static_cast<std::byte>(message.size() >> 8U));
    out.push_back(static_cast<std::byte>(message.size() & 0xFFU));
    out.insert(out.end(), message.begin(), message.end());
  }
  return out;
}

const auto kNoProgress = [](const itch::DriveResult&, std::uint64_t) {};

TEST(Drive, ReadsToTheEnd) {
  const auto data = framed({blank(MsgType::SystemEvent, 12), blank(MsgType::OrderDelete, 19)});
  Recorder recorder;
  const auto result = itch::drive(data, recorder, {}, kNoProgress);
  EXPECT_EQ(result.messages, 2U);
  EXPECT_EQ(result.malformed, 0U);
  EXPECT_EQ(result.offset, data.size());
  EXPECT_FALSE(result.truncated);
  EXPECT_FALSE(result.stopped);
}

TEST(Drive, StrictStopsAtTheMalformedFrame) {
  const auto data = framed({blank(MsgType::SystemEvent, 12), blank(MsgType::OrderDelete, 18),
                            blank(MsgType::OrderDelete, 19)});
  Recorder recorder;
  const auto result = itch::drive(data, recorder, {.strict = true}, kNoProgress);
  EXPECT_EQ(result.messages, 2U);
  EXPECT_EQ(result.malformed, 1U);
  EXPECT_EQ(result.offset, 14U);
  EXPECT_TRUE(result.stopped);
  EXPECT_EQ(recorder.seen.size(), 1U);
}

TEST(Drive, LenientCountsAndSkips) {
  const auto data = framed({blank(static_cast<MsgType>('z'), 4), blank(MsgType::OrderDelete, 19)});
  Recorder recorder;
  const auto result = itch::drive(data, recorder, {.strict = false}, kNoProgress);
  EXPECT_EQ(result.messages, 2U);
  EXPECT_EQ(result.malformed, 1U);
  EXPECT_EQ(recorder.seen.size(), 1U);
}

TEST(Drive, ReportsTruncationWithTheOffsetOfTheFrame) {
  auto data = framed({blank(MsgType::SystemEvent, 12)});
  data.push_back(std::byte{0});
  data.push_back(std::byte{19});
  data.push_back(std::byte{'D'});
  Recorder recorder;
  const auto result = itch::drive(data, recorder, {}, kNoProgress);
  EXPECT_TRUE(result.truncated);
  EXPECT_EQ(result.messages, 1U);
  EXPECT_EQ(result.offset, 14U);
}

TEST(Drive, LimitStopsEarly) {
  const auto data = framed({blank(MsgType::SystemEvent, 12), blank(MsgType::SystemEvent, 12)});
  Recorder recorder;
  const auto result = itch::drive(data, recorder, {.limit = 1}, kNoProgress);
  EXPECT_EQ(result.messages, 1U);
  EXPECT_EQ(result.offset, 14U);
}

TEST(Drive, ProgressRunsAtTheCadence) {
  const auto data = framed({blank(MsgType::SystemEvent, 12), blank(MsgType::SystemEvent, 12),
                            blank(MsgType::SystemEvent, 12)});
  Recorder recorder;
  std::vector<std::uint64_t> offsets;
  itch::drive(data, recorder, {.progress_every = 2},
              [&](const itch::DriveResult&, std::uint64_t offset) { offsets.push_back(offset); });
  EXPECT_EQ(offsets, std::vector<std::uint64_t>{28U});
}

}  // namespace
