#include "itch/net/mold_receiver.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using itch::MoldReceiver;
namespace mold = itch::mold;

constexpr std::int64_t kMilli = 1'000'000;

/// Packet with one single-byte message per sequence, whose byte is the sequence number.
std::vector<std::byte> data(std::uint64_t seq, std::uint16_t count, const char* session = "ABC") {
  std::vector<std::byte> out(mold::kHeaderBytes);
  mold::encode(out.data(), mold::make_session(session), seq, count);
  for (std::uint64_t i = 0; i < count; ++i) {
    out.push_back(std::byte{0});
    out.push_back(std::byte{1});
    out.push_back(static_cast<std::byte>(seq + i));
  }
  return out;
}

std::vector<std::byte> control(std::uint64_t seq, std::uint16_t count) {
  std::vector<std::byte> out(mold::kHeaderBytes);
  mold::encode(out.data(), mold::make_session("ABC"), seq, count);
  return out;
}

class ReceiverTest : public ::testing::Test {
 protected:
  MoldReceiver::Status feed(const std::vector<std::byte>& packet, std::int64_t now = 0) {
    return receiver_.on_packet(
        packet, now,
        [this](std::span<const std::byte> message) {
          delivered_.push_back(std::to_integer<int>(message[0]));
        },
        [this](std::uint64_t seq, std::uint16_t count) { asked_.emplace_back(seq, count); });
  }
  MoldReceiver::Status tick(std::int64_t now) {
    return receiver_.on_tick(
        now, [this](std::uint64_t seq, std::uint16_t count) { asked_.emplace_back(seq, count); });
  }

  itch::ReceiverOptions options_{
      .max_buffered_packets = 4, .rerequest_timeout_ns = 50 * kMilli, .rerequest_max_retries = 2};
  MoldReceiver receiver_{options_};
  std::vector<int> delivered_;
  std::vector<std::pair<std::uint64_t, std::uint16_t>> asked_;
};

TEST_F(ReceiverTest, InOrderPacketsAreDelivered) {
  feed(data(1, 3));
  feed(data(4, 2));
  EXPECT_EQ(delivered_, (std::vector<int>{1, 2, 3, 4, 5}));
  EXPECT_TRUE(asked_.empty());
  EXPECT_EQ(receiver_.expected(), 6U);
  EXPECT_EQ(receiver_.stats().gaps, 0U);
}

TEST_F(ReceiverTest, GapIsBufferedRequestedAndFilled) {
  feed(data(1, 2));
  feed(data(6, 2));
  EXPECT_EQ(delivered_, (std::vector<int>{1, 2}));
  ASSERT_EQ(asked_.size(), 1U);
  EXPECT_EQ(asked_[0], (std::pair<std::uint64_t, std::uint16_t>{3, 3}));

  feed(data(3, 3));  // The retransmission.
  EXPECT_EQ(delivered_, (std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
  EXPECT_EQ(receiver_.stats().gaps, 1U);
  EXPECT_EQ(receiver_.stats().gap_messages, 3U);
  EXPECT_EQ(receiver_.stats().largest_gap, 3U);
  EXPECT_EQ(asked_.size(), 1U);
}

TEST_F(ReceiverTest, PartialRetransmissionAsksForTheRest) {
  feed(data(1, 1));
  feed(data(6, 1));
  feed(data(2, 2));
  EXPECT_EQ(receiver_.expected(), 4U);
  ASSERT_EQ(asked_.size(), 2U);
  EXPECT_EQ(asked_[1], (std::pair<std::uint64_t, std::uint16_t>{4, 2}));
  feed(data(4, 2));
  EXPECT_EQ(delivered_, (std::vector<int>{1, 2, 3, 4, 5, 6}));
}

TEST_F(ReceiverTest, DuplicatesAreDroppedAndOverlapsTrimmed) {
  feed(data(1, 3));
  feed(data(1, 3));
  EXPECT_EQ(receiver_.stats().duplicate_packets, 1U);
  feed(data(2, 4));
  EXPECT_EQ(delivered_, (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST_F(ReceiverTest, OutOfOrderPacketsComeOutInOrder) {
  feed(data(5, 1));
  feed(data(3, 1));
  feed(data(2, 1));
  feed(data(4, 1));
  EXPECT_TRUE(delivered_.empty());
  feed(data(1, 1));
  EXPECT_EQ(delivered_, (std::vector<int>{1, 2, 3, 4, 5}));
}

TEST_F(ReceiverTest, BufferIsBoundedAndOverflowIsRecoverable) {
  feed(data(2, 1));
  feed(data(3, 1));
  feed(data(4, 1));
  feed(data(5, 1));
  feed(data(6, 1));
  EXPECT_EQ(receiver_.stats().overflow_packets, 1U);
  feed(data(1, 1));
  EXPECT_EQ(delivered_, (std::vector<int>{1, 2, 3, 4, 5}));
  feed(data(6, 1));
  EXPECT_EQ(delivered_.back(), 6);
}

TEST_F(ReceiverTest, TimeoutRepeatsTheRequest) {
  feed(data(3, 1), 0);
  ASSERT_EQ(asked_.size(), 1U);
  EXPECT_EQ(tick(10 * kMilli), MoldReceiver::Status::Running);
  EXPECT_EQ(asked_.size(), 1U);
  EXPECT_EQ(tick(60 * kMilli), MoldReceiver::Status::Running);
  EXPECT_EQ(asked_.size(), 2U);
  EXPECT_EQ(receiver_.stats().timeouts, 1U);
}

TEST_F(ReceiverTest, GapOutsideTheWindowFailsAfterTheRetries) {
  feed(data(3, 1), 0);
  EXPECT_EQ(tick(60 * kMilli), MoldReceiver::Status::Running);
  EXPECT_EQ(tick(120 * kMilli), MoldReceiver::Status::Running);
  EXPECT_EQ(tick(180 * kMilli), MoldReceiver::Status::Unrecoverable);
  EXPECT_EQ(receiver_.unrecovered_from(), 1U);
}

TEST_F(ReceiverTest, ProgressResetsTheRetries) {
  feed(data(5, 1), 0);
  tick(60 * kMilli);
  tick(120 * kMilli);
  feed(data(1, 1), 130 * kMilli);
  EXPECT_EQ(tick(200 * kMilli), MoldReceiver::Status::Running);
  EXPECT_EQ(tick(260 * kMilli), MoldReceiver::Status::Running);
  EXPECT_EQ(tick(320 * kMilli), MoldReceiver::Status::Unrecoverable);
}

TEST_F(ReceiverTest, HeartbeatRevealsLossAtTheTail) {
  feed(data(1, 2));
  feed(control(5, mold::kHeartbeat));
  EXPECT_EQ(receiver_.stats().heartbeats, 1U);
  ASSERT_EQ(asked_.size(), 1U);
  EXPECT_EQ(asked_[0], (std::pair<std::uint64_t, std::uint16_t>{3, 2}));
}

TEST_F(ReceiverTest, EndOfSessionFinishesOnceEverythingArrived) {
  feed(data(1, 2));
  EXPECT_EQ(feed(control(3, mold::kEndOfSession)), MoldReceiver::Status::Finished);
}

TEST_F(ReceiverTest, EndOfSessionWaitsForMissingMessages) {
  feed(data(1, 2));
  EXPECT_EQ(feed(control(5, mold::kEndOfSession)), MoldReceiver::Status::Running);
  EXPECT_EQ(asked_.size(), 1U);
  EXPECT_EQ(feed(data(3, 2)), MoldReceiver::Status::Finished);
}

TEST_F(ReceiverTest, EmptySessionFinishesAtOnce) {
  EXPECT_EQ(feed(control(1, mold::kEndOfSession)), MoldReceiver::Status::Finished);
}

TEST_F(ReceiverTest, OtherSessionsAndBadPacketsAreCounted) {
  feed(data(1, 1));
  feed(data(2, 1, "OTHER"));
  EXPECT_EQ(receiver_.stats().wrong_session, 1U);
  feed(std::vector<std::byte>(5));
  EXPECT_EQ(receiver_.stats().malformed_packets, 1U);
  feed(data(0, 1));
  EXPECT_EQ(receiver_.stats().malformed_packets, 2U);
  auto empty_blocks = control(2, 3);
  feed(empty_blocks);
  EXPECT_EQ(receiver_.stats().malformed_packets, 3U);
  auto wrap = control(~std::uint64_t{0}, 5);
  feed(wrap);
  EXPECT_EQ(receiver_.stats().malformed_packets, 4U);
  EXPECT_EQ(delivered_, (std::vector<int>{1}));
}

TEST_F(ReceiverTest, TimelineBucketsBySecond) {
  feed(data(1, 2), 0);
  feed(data(4, 1), 1'500 * kMilli);
  const auto& timeline = receiver_.stats().timeline;
  ASSERT_EQ(timeline.size(), 2U);
  EXPECT_EQ(timeline[0].messages, 2U);
  EXPECT_EQ(timeline[1].gaps, 1U);
  EXPECT_EQ(timeline[1].rerequests, 1U);
}

}  // namespace
