#include "itch/net/moldudp64.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "itch/net/drop_plan.hpp"

namespace {

using itch::DropPlan;
namespace mold = itch::mold;

std::vector<std::byte> packet(std::uint64_t seq, std::uint16_t count,
                              const std::vector<std::string>& messages) {
  std::vector<std::byte> out(mold::kHeaderBytes);
  mold::encode(out.data(), mold::make_session("ABC"), seq, count);
  for (const std::string& message : messages) {
    out.push_back(static_cast<std::byte>(message.size() >> 8U));
    out.push_back(static_cast<std::byte>(message.size() & 0xFFU));
    for (const char c : message) {
      out.push_back(static_cast<std::byte>(c));
    }
  }
  return out;
}

TEST(Mold, SessionIsPaddedToTenCharacters) {
  EXPECT_EQ(std::string(mold::make_session("ABC").data(), 10), "ABC       ");
  EXPECT_EQ(std::string(mold::make_session("ABCDEFGHIJKL").data(), 10), "ABCDEFGHIJ");
}

TEST(Mold, HeaderRoundTripsAndIsBigEndian) {
  const auto bytes = packet(0x0102030405060708ULL, 0x0A0B, {});
  EXPECT_EQ(std::to_integer<int>(bytes[10]), 0x01);
  EXPECT_EQ(std::to_integer<int>(bytes[17]), 0x08);
  EXPECT_EQ(std::to_integer<int>(bytes[18]), 0x0A);
  mold::PacketHeader header;
  ASSERT_TRUE(mold::decode_header(bytes, header));
  EXPECT_EQ(header.session, mold::make_session("ABC"));
  EXPECT_EQ(header.seq, 0x0102030405060708ULL);
  EXPECT_EQ(header.count, 0x0A0B);
}

TEST(Mold, ShortPacketHasNoHeader) {
  const std::array<std::byte, 19> bytes{};
  mold::PacketHeader header;
  EXPECT_FALSE(mold::decode_header(bytes, header));
}

TEST(Mold, RequestMustBeExactlyTwentyBytes) {
  auto bytes = packet(7, 3, {});
  mold::RetransRequest request;
  ASSERT_TRUE(mold::decode_request(bytes, request));
  EXPECT_EQ(request.seq, 7U);
  EXPECT_EQ(request.count, 3);
  bytes.push_back(std::byte{0});
  EXPECT_FALSE(mold::decode_request(bytes, request));
}

TEST(Mold, MessagesAreWalkedInOrder) {
  std::vector<std::string> seen;
  const auto bytes = packet(1, 3, {"one", "", "three"});
  EXPECT_TRUE(mold::for_each_message(bytes, [&](std::span<const std::byte> message) {
    seen.emplace_back(reinterpret_cast<const char*>(message.data()), message.size());
  }));
  EXPECT_EQ(seen, (std::vector<std::string>{"one", "", "three"}));
}

TEST(Mold, HeartbeatHasNoMessages) {
  int calls = 0;
  EXPECT_TRUE(mold::for_each_message(packet(5, mold::kHeartbeat, {}),
                                     [&](std::span<const std::byte>) { ++calls; }));
  EXPECT_EQ(calls, 0);
}

TEST(Mold, EndOfSessionIsNotWalked) {
  int calls = 0;
  EXPECT_FALSE(mold::for_each_message(packet(5, mold::kEndOfSession, {}),
                                      [&](std::span<const std::byte>) { ++calls; }));
  EXPECT_EQ(calls, 0);
}

TEST(Mold, BlockPastTheEndStopsTheWalk) {
  auto bytes = packet(1, 2, {"one", "two"});
  bytes.pop_back();
  int calls = 0;
  EXPECT_FALSE(mold::for_each_message(bytes, [&](std::span<const std::byte>) { ++calls; }));
  EXPECT_EQ(calls, 1);
}

TEST(Mold, LoneLengthByteStopsTheWalk) {
  auto bytes = packet(1, 2, {"one"});
  bytes.push_back(std::byte{0});
  EXPECT_FALSE(mold::for_each_message(bytes, [](std::span<const std::byte>) {}));
}

TEST(Mold, CountBelowTheBlocksIsReported) {
  EXPECT_FALSE(
      mold::for_each_message(packet(1, 1, {"one", "two"}), [](std::span<const std::byte>) {}));
}

TEST(DropPlan, BurstParses) {
  const auto burst = DropPlan::parse_burst("5000000:10000");
  EXPECT_EQ(burst.at_seq, 5'000'000U);
  EXPECT_EQ(burst.packets, 10'000U);
  EXPECT_THROW(static_cast<void>(DropPlan::parse_burst("5000000")), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(DropPlan::parse_burst("x:1")), std::invalid_argument);
  EXPECT_THROW(static_cast<void>(DropPlan::parse_burst("1:")), std::invalid_argument);
}

TEST(DropPlan, EmptyPlanDropsNothing) {
  DropPlan plan;
  for (std::uint64_t seq = 1; seq < 1000; seq += 10) {
    EXPECT_FALSE(plan.should_drop(seq, 10));
  }
  EXPECT_EQ(plan.dropped(), 0U);
}

TEST(DropPlan, ExplicitSequencesDropTheirPackets) {
  DropPlan plan(1, 0.0, {}, {17, 18});
  EXPECT_FALSE(plan.should_drop(1, 10));
  EXPECT_TRUE(plan.should_drop(11, 10));
  EXPECT_FALSE(plan.should_drop(21, 10));
}

TEST(DropPlan, BurstDropsConsecutivePacketsFromItsSequence) {
  DropPlan plan(1, 0.0, {{.at_seq = 25, .packets = 3}}, {});
  EXPECT_FALSE(plan.should_drop(1, 10));
  EXPECT_FALSE(plan.should_drop(11, 10));
  EXPECT_TRUE(plan.should_drop(21, 10));
  EXPECT_TRUE(plan.should_drop(31, 10));
  EXPECT_TRUE(plan.should_drop(41, 10));
  EXPECT_FALSE(plan.should_drop(51, 10));
  EXPECT_EQ(plan.dropped(), 3U);
}

TEST(DropPlan, RandomDropsAreSeededAndNearTheRate) {
  const auto run = [](std::uint64_t seed) {
    DropPlan plan(seed, 0.01, {}, {});
    std::vector<bool> drops;
    for (std::uint64_t seq = 1; seq <= 100'000; ++seq) {
      drops.push_back(plan.should_drop(seq, 1));
    }
    return std::pair(drops, plan.dropped());
  };
  const auto first = run(42);
  EXPECT_EQ(first, run(42));
  EXPECT_NE(first.first, run(43).first);
  EXPECT_NEAR(static_cast<double>(first.second), 1000.0, 150.0);
}

}  // namespace
