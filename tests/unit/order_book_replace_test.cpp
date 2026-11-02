// Replace semantics, ITCH 5.0 section 1.4.5: docs/protocol-notes.md#replace.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "itch/book/order_book.hpp"

namespace {

using itch::Level;
using itch::OrderBook;
using itch::OrderStore;
using itch::Side;

class ReplaceTest : public ::testing::Test {
 protected:
  [[nodiscard]] std::vector<std::uint64_t> queue(Side side) const {
    std::vector<std::uint64_t> refs;
    book_.for_each_order(side, [&refs](const itch::OrderNode& node) { refs.push_back(node.ref); });
    return refs;
  }

  OrderStore store_;
  OrderBook book_{store_};
};

TEST_F(ReplaceTest, SamePriceLosesPriority) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Bid, 100, 10);
  book_.replace(1, 3, 100, 10);
  EXPECT_EQ(queue(Side::Bid), (std::vector<std::uint64_t>{2, 3}));
  EXPECT_EQ(book_.best(Side::Bid), (Level{100, 20, 2}));
}

TEST_F(ReplaceTest, DifferentPriceMovesLevelAndGoesToTheBack) {
  book_.add(1, Side::Ask, 105, 10);
  book_.add(2, Side::Ask, 103, 10);
  book_.add(3, Side::Ask, 103, 10);
  book_.replace(1, 4, 103, 25);
  EXPECT_EQ(queue(Side::Ask), (std::vector<std::uint64_t>{2, 3, 4}));
  EXPECT_EQ(book_.best(Side::Ask), (Level{103, 45, 3}));
  EXPECT_EQ(book_.level_count(Side::Ask), 1U);
}

TEST_F(ReplaceTest, OldReferenceIsGoneAndNewOneWorks) {
  book_.add(1, Side::Bid, 100, 10);
  book_.replace(1, 2, 101, 7);
  EXPECT_EQ(store_.find(1), itch::kNoOrder);
  EXPECT_EQ(book_.execute(2, 3), 4);
  EXPECT_EQ(book_.execute(1, 1), -1);
}

TEST_F(ReplaceTest, KeepsSideAndAttribution) {
  book_.add(1, Side::Ask, 100, 10);
  const std::uint32_t index = store_.find(1);
  store_[index].mpid = 0x56495254;
  book_.replace(1, 2, 99, 10);
  const std::uint32_t moved = store_.find(2);
  EXPECT_EQ(store_[moved].mpid, 0x56495254U);
  EXPECT_EQ(store_[moved].side, Side::Ask);
}

TEST_F(ReplaceTest, AcrossTheSpreadLeavesTheBookCrossed) {
  // The handler reports what the feed says. It never matches orders itself.
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Ask, 102, 10);
  book_.replace(1, 3, 103, 10);
  EXPECT_TRUE(book_.crossed());
}

TEST_F(ReplaceTest, NewReferenceAlreadyLiveLeavesTheOrderAlone) {
  book_.add(1, Side::Bid, 100, 10);
  book_.add(2, Side::Bid, 99, 10);
  book_.replace(1, 2, 101, 5);
  EXPECT_EQ(book_.best(Side::Bid), (Level{100, 10, 1}));
  EXPECT_EQ(store_.live(), 2U);
}

}  // namespace
