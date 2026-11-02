#include "itch/book/order_store.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

namespace {

using itch::kNoOrder;
using itch::OpenAddressingMap;
using itch::OrderMapKind;
using itch::OrderStore;

TEST(OpenAddressingMap, InsertFindErase) {
  OpenAddressingMap map(16);
  EXPECT_EQ(map.find(5), kNoOrder);
  EXPECT_TRUE(map.insert(5, 50));
  EXPECT_FALSE(map.insert(5, 51));
  EXPECT_EQ(map.find(5), 50U);
  EXPECT_EQ(map.size(), 1U);
  EXPECT_TRUE(map.erase(5));
  EXPECT_FALSE(map.erase(5));
  EXPECT_EQ(map.find(5), kNoOrder);
}

TEST(OpenAddressingMap, ZeroAndMaximumKeysAreOrdinaryKeys) {
  OpenAddressingMap map(16);
  EXPECT_TRUE(map.insert(0, 1));
  EXPECT_TRUE(map.insert(~std::uint64_t{0}, 2));
  EXPECT_EQ(map.find(0), 1U);
  EXPECT_EQ(map.find(~std::uint64_t{0}), 2U);
}

TEST(OpenAddressingMap, GrowsPastItsHint) {
  OpenAddressingMap map(8);
  const std::size_t initial = map.capacity();
  for (std::uint64_t key = 0; key < 10'000; ++key) {
    ASSERT_TRUE(map.insert(key, static_cast<std::uint32_t>(key)));
  }
  EXPECT_GT(map.capacity(), initial);
  for (std::uint64_t key = 0; key < 10'000; ++key) {
    ASSERT_EQ(map.find(key), key);
  }
}

TEST(OpenAddressingMap, ClearForgetsEverythingAndKeepsCapacity) {
  OpenAddressingMap map(64);
  map.insert(9, 1);
  const std::size_t capacity = map.capacity();
  map.clear();
  EXPECT_EQ(map.size(), 0U);
  EXPECT_EQ(map.capacity(), capacity);
  EXPECT_EQ(map.find(9), kNoOrder);
}

TEST(OpenAddressingMap, BackwardShiftKeepsEveryProbeRunReachable) {
  // Random churn against std::unordered_map, small enough that probe runs collide constantly.
  OpenAddressingMap map(32);
  std::unordered_map<std::uint64_t, std::uint32_t> model;
  std::mt19937_64 rng(11);
  for (int step = 0; step < 200'000; ++step) {
    const std::uint64_t key = rng() % 64;
    if (rng() % 2 == 0) {
      const auto value = static_cast<std::uint32_t>(step);
      ASSERT_EQ(map.insert(key, value), model.try_emplace(key, value).second);
    } else {
      ASSERT_EQ(map.erase(key), model.erase(key) == 1);
    }
    const std::uint64_t probe = rng() % 64;
    const auto found = model.find(probe);
    ASSERT_EQ(map.find(probe), found == model.end() ? kNoOrder : found->second);
  }
  EXPECT_EQ(map.size(), model.size());
}

TEST(OrderMapKind, ParsesConfigNames) {
  EXPECT_EQ(itch::parse_order_map("open_addressing"), OrderMapKind::OpenAddressing);
  EXPECT_EQ(itch::parse_order_map("unordered_dense"), OrderMapKind::UnorderedDense);
  EXPECT_EQ(itch::parse_order_map("std"), OrderMapKind::Std);
  EXPECT_THROW(static_cast<void>(itch::parse_order_map("btree")), std::invalid_argument);
}

class OrderStoreTest : public ::testing::TestWithParam<OrderMapKind> {};

TEST_P(OrderStoreTest, AllocateFindRelease) {
  OrderStore store(GetParam(), 8);
  const std::uint32_t a = store.allocate(100);
  ASSERT_NE(a, kNoOrder);
  EXPECT_EQ(store[a].ref, 100U);
  EXPECT_EQ(store.find(100), a);
  EXPECT_EQ(store.allocate(100), kNoOrder);
  EXPECT_EQ(store.live(), 1U);

  store.release(a);
  EXPECT_EQ(store.find(100), kNoOrder);
  EXPECT_EQ(store.live(), 0U);
  EXPECT_EQ(store.peak_live(), 1U);
}

TEST_P(OrderStoreTest, ReleasedNodesAreReused) {
  OrderStore store(GetParam(), 8);
  const std::uint32_t a = store.allocate(1);
  store.release(a);
  EXPECT_EQ(store.allocate(2), a);
}

TEST_P(OrderStoreTest, RenameKeepsTheNode) {
  OrderStore store(GetParam(), 8);
  const std::uint32_t a = store.allocate(1);
  const std::uint32_t b = store.allocate(2);
  store[a].shares = 77;
  EXPECT_FALSE(store.rename(a, 2));
  EXPECT_TRUE(store.rename(a, 3));
  EXPECT_EQ(store.find(1), kNoOrder);
  EXPECT_EQ(store.find(3), a);
  EXPECT_EQ(store[a].shares, 77U);
  EXPECT_EQ(store.find(2), b);
}

TEST_P(OrderStoreTest, ClearEmptiesTheStore) {
  OrderStore store(GetParam(), 8);
  store.allocate(1);
  store.clear();
  EXPECT_EQ(store.live(), 0U);
  EXPECT_EQ(store.find(1), kNoOrder);
  EXPECT_NE(store.allocate(1), kNoOrder);
}

INSTANTIATE_TEST_SUITE_P(Maps, OrderStoreTest,
                         ::testing::Values(OrderMapKind::OpenAddressing,
                                           OrderMapKind::UnorderedDense, OrderMapKind::Std));

}  // namespace
