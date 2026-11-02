#include "itch/book/order_book.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using itch::Level;
using itch::OrderBook;
using itch::OrderStore;
using itch::Side;

class OrderBookTest : public ::testing::Test {
 protected:
  [[nodiscard]] std::vector<Level> depth(Side side) const {
    std::array<Level, 16> out{};
    const std::size_t count = book_.levels(side, out);
    return {out.begin(), out.begin() + static_cast<std::ptrdiff_t>(count)};
  }
  [[nodiscard]] std::vector<std::uint64_t> queue(Side side) const {
    std::vector<std::uint64_t> refs;
    book_.for_each_order(side, [&refs](const itch::OrderNode& node) { refs.push_back(node.ref); });
    return refs;
  }

  OrderStore store_;
  OrderBook book_{store_};
};

TEST_F(OrderBookTest, EmptyBookHasNoBest) {
  EXPECT_FALSE(book_.best(Side::Bid));
  EXPECT_FALSE(book_.best(Side::Ask));
  EXPECT_FALSE(book_.crossed());
  EXPECT_FALSE(book_.locked());
  EXPECT_FALSE(book_.touching());
}

TEST_F(OrderBookTest, LevelsAggregateAndSortBestFirst) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Bid, 101, 20);
  book_.add(3, Side::Bid, 100, 30);
  book_.add(4, Side::Ask, 103, 5);
  book_.add(5, Side::Ask, 102, 7);

  EXPECT_EQ(depth(Side::Bid), (std::vector<Level>{{101, 20, 1}, {100, 40, 2}}));
  EXPECT_EQ(depth(Side::Ask), (std::vector<Level>{{102, 7, 1}, {103, 5, 1}}));
  EXPECT_EQ(book_.best(Side::Bid), (Level{101, 20, 1}));
  EXPECT_EQ(book_.best(Side::Ask), (Level{102, 7, 1}));
  EXPECT_EQ(book_.level_count(Side::Bid), 2U);
}

TEST_F(OrderBookTest, QueueIsFifoWithinALevel) {
  book_.add(1, Side::Ask, 100, 10);
  book_.add(2, Side::Ask, 100, 10);
  book_.add(3, Side::Ask, 100, 10);
  EXPECT_EQ(queue(Side::Ask), (std::vector<std::uint64_t>{1, 2, 3}));
}

TEST_F(OrderBookTest, DuplicateReferenceIsIgnored) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(1, Side::Ask, 200, 10);
  EXPECT_TRUE(depth(Side::Ask).empty());
  EXPECT_EQ(store_.live(), 1U);
}

TEST_F(OrderBookTest, PartialExecutionKeepsQueuePosition) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Bid, 100, 10);
  EXPECT_EQ(book_.execute(1, 4), 6);
  EXPECT_EQ(queue(Side::Bid), (std::vector<std::uint64_t>{1, 2}));
  EXPECT_EQ(book_.best(Side::Bid), (Level{100, 16, 2}));
}

TEST_F(OrderBookTest, FullExecutionRemovesTheOrderAndEmptyLevel) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Bid, 99, 10);
  EXPECT_EQ(book_.execute(1, 10), 0);
  EXPECT_EQ(store_.find(1), itch::kNoOrder);
  EXPECT_EQ(book_.best(Side::Bid), (Level{99, 10, 1}));
}

TEST_F(OrderBookTest, ExecutionClampsToWhatIsLeft) {
  book_.add(1, Side::Bid, 100, 10);
  EXPECT_EQ(book_.execute(1, 25), 0);
  EXPECT_FALSE(book_.best(Side::Bid));
}

TEST_F(OrderBookTest, UnknownReferencesAreReported) {
  EXPECT_EQ(book_.execute(9, 1), -1);
  EXPECT_EQ(book_.cancel(9, 1), -1);
  book_.remove(9);
  book_.replace(9, 10, 100, 1);
  EXPECT_EQ(store_.live(), 0U);
}

TEST_F(OrderBookTest, CancelToZeroRemoves) {
  book_.add(1, Side::Ask, 100, 10);
  EXPECT_EQ(book_.cancel(1, 3), 7);
  EXPECT_EQ(book_.cancel(1, 7), 0);
  EXPECT_FALSE(book_.best(Side::Ask));
}

TEST_F(OrderBookTest, RemovingTheMiddleOfAQueueRelinks) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Bid, 100, 20);
  book_.add(3, Side::Bid, 100, 30);
  book_.remove(2);
  EXPECT_EQ(queue(Side::Bid), (std::vector<std::uint64_t>{1, 3}));
  EXPECT_EQ(book_.best(Side::Bid), (Level{100, 40, 2}));
  book_.remove(1);
  EXPECT_EQ(queue(Side::Bid), (std::vector<std::uint64_t>{3}));
  book_.remove(3);
  EXPECT_FALSE(book_.best(Side::Bid));
}

TEST_F(OrderBookTest, CrossedAndLocked) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Ask, 101, 10);
  EXPECT_FALSE(book_.touching());
  book_.add(3, Side::Bid, 101, 10);
  EXPECT_TRUE(book_.locked());
  EXPECT_FALSE(book_.crossed());
  EXPECT_TRUE(book_.touching());
  book_.add(4, Side::Bid, 102, 10);
  EXPECT_TRUE(book_.crossed());
  EXPECT_FALSE(book_.locked());
}

TEST_F(OrderBookTest, ManyLevelsUseTheBinarySearchPath) {
  for (std::uint64_t i = 0; i < 200; ++i) {
    book_.add(i + 1, Side::Bid, static_cast<std::int64_t>(1000 + (i * 2)), 10);
    book_.add(i + 1001, Side::Ask, static_cast<std::int64_t>(3000 + (i * 2)), 10);
  }
  book_.add(5000, Side::Bid, 1001, 10);  // Far from the touch, a new level.
  book_.add(5001, Side::Bid, 1002, 10);  // Far from the touch, an existing level.
  book_.add(5002, Side::Ask, 3199, 10);
  book_.add(5003, Side::Bid, 999, 10);  // Beyond the worst level.
  EXPECT_EQ(book_.level_count(Side::Bid), 202U);
  EXPECT_EQ(book_.level_count(Side::Ask), 201U);
  book_.remove(5000);
  book_.remove(5003);
  EXPECT_EQ(book_.level_count(Side::Bid), 200U);
  EXPECT_EQ(book_.execute(2, 5), 5);  // Order 2 rests at 1002 with order 5001.
  std::array<Level, 300> all{};
  const std::size_t count = book_.levels(Side::Bid, all);
  EXPECT_EQ(all[count - 2], (Level{1002, 15, 2}));
}

TEST_F(OrderBookTest, ClearEmptiesBothSides) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Ask, 101, 10);
  book_.reserve(32);
  book_.clear();
  EXPECT_FALSE(book_.best(Side::Bid));
  EXPECT_FALSE(book_.best(Side::Ask));
}

}  // namespace
