#include "itch/book/book_manager.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "itch/proto/parse.hpp"
#include "message_bytes.hpp"

namespace {

using itch::BookManager;
using itch::Level;
using itch::Side;
using itch::test::MessageBytes;

constexpr std::uint64_t kSecond = 1'000'000'000;
constexpr std::uint64_t at(std::uint64_t hours, std::uint64_t minutes) {
  return ((hours * 3600) + (minutes * 60)) * kSecond;
}

MessageBytes system_event(char code, std::uint64_t ts) {
  MessageBytes m('S', 0, 0, ts);
  m.alpha(code);
  return m;
}
MessageBytes directory(std::uint16_t locate, std::string_view symbol, char category) {
  MessageBytes m('R', locate, 0, 0);
  m.text(symbol, 8)
      .alpha(category)
      .alpha('N')
      .u32(100)
      .alpha('N')
      .alpha('C')
      .text("Z", 2)
      .alpha('P')
      .alpha('N')
      .alpha('N')
      .alpha('1')
      .alpha('N')
      .u32(1)
      .alpha('N');
  return m;
}
MessageBytes action(std::uint16_t locate, char state, std::uint64_t ts) {
  MessageBytes m('H', locate, 0, ts);
  m.text("SYM", 8).alpha(state).alpha(' ').text("", 4);
  return m;
}
MessageBytes add(std::uint16_t locate, std::uint64_t ref, char side, std::uint32_t shares,
                 std::uint32_t price, std::uint64_t ts) {
  MessageBytes m('A', locate, 0, ts);
  m.u64(ref).alpha(side).u32(shares).text("SYM", 8).u32(price);
  return m;
}
MessageBytes add_mpid(std::uint16_t locate, std::uint64_t ref, char side, std::uint32_t shares,
                      std::uint32_t price, std::uint64_t ts) {
  MessageBytes m('F', locate, 0, ts);
  m.u64(ref).alpha(side).u32(shares).text("SYM", 8).u32(price).text("VIRT", 4);
  return m;
}
MessageBytes executed(std::uint64_t ref, std::uint32_t shares, std::uint64_t ts) {
  MessageBytes m('E', 1, 0, ts);
  m.u64(ref).u32(shares).u64(ref + 1000);
  return m;
}
MessageBytes executed_price(std::uint64_t ref, std::uint32_t shares, char printable,
                            std::uint64_t ts) {
  MessageBytes m('C', 1, 0, ts);
  m.u64(ref).u32(shares).u64(ref + 2000).alpha(printable).u32(100);
  return m;
}
MessageBytes cancelled(std::uint64_t ref, std::uint32_t shares, std::uint64_t ts) {
  MessageBytes m('X', 1, 0, ts);
  m.u64(ref).u32(shares);
  return m;
}
MessageBytes deleted(std::uint64_t ref, std::uint64_t ts) {
  MessageBytes m('D', 1, 0, ts);
  m.u64(ref);
  return m;
}
MessageBytes replaced(std::uint64_t old_ref, std::uint64_t new_ref, std::uint32_t shares,
                      std::uint32_t price, std::uint64_t ts) {
  MessageBytes m('U', 1, 0, ts);
  m.u64(old_ref).u64(new_ref).u32(shares).u32(price);
  return m;
}
MessageBytes hidden_trade(std::uint32_t shares, std::uint64_t ts) {
  MessageBytes m('P', 1, 0, ts);
  m.u64(0).alpha('B').u32(shares).text("SYM", 8).u32(100).u64(77);
  return m;
}
MessageBytes cross(char type, std::uint64_t shares, std::uint64_t ts) {
  MessageBytes m('Q', 1, 0, ts);
  m.u64(shares).text("SYM", 8).u32(100).u64(78).alpha(type);
  return m;
}
MessageBytes noii(char cross_type, std::uint64_t ts) {
  MessageBytes m('I', 1, 0, ts);
  m.u64(10).u64(5).alpha('B').text("SYM", 8).u32(99).u32(100).u32(101).alpha(cross_type).alpha('L');
  return m;
}
MessageBytes broken(std::uint64_t match, std::uint64_t ts) {
  MessageBytes m('B', 1, 0, ts);
  m.u64(match);
  return m;
}

class BookManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    manager_ = std::make_unique<BookManager>(itch::BookConfig{.order_capacity_hint = 64});
    feed(directory(1, "SYM", 'Q'));
    feed(system_event('O', 0));
    feed(system_event('S', at(4, 0)));
    feed(action(1, 'T', at(4, 0)));
  }

  void feed(const MessageBytes& message) {
    ASSERT_EQ(itch::parse(message.bytes(), *manager_), itch::ParseStatus::Ok);
  }

  [[nodiscard]] const itch::SymbolCounters& symbol() const { return manager_->symbol_counters(1); }
  [[nodiscard]] const itch::Counters& counters() const { return manager_->counters(); }

  std::unique_ptr<BookManager> manager_;
};

TEST_F(BookManagerTest, BookLookupBySymbol) {
  EXPECT_NE(manager_->book("SYM"), nullptr);
  EXPECT_EQ(manager_->book("NOPE"), nullptr);
}

TEST_F(BookManagerTest, CountersFollowEveryOrderEvent) {
  feed(add(1, 1, 'B', 100, 990, at(5, 0)));
  feed(add_mpid(1, 2, 'B', 100, 980, at(5, 0)));
  feed(add(1, 3, 'S', 100, 1010, at(5, 0)));
  feed(add(1, 4, 'S', 100, 1020, at(5, 0)));
  feed(executed(1, 30, at(5, 1)));
  feed(executed_price(1, 20, 'Y', at(5, 1)));
  feed(executed_price(1, 10, 'N', at(5, 1)));
  feed(cancelled(2, 40, at(5, 2)));
  feed(deleted(3, at(5, 3)));
  feed(replaced(4, 5, 60, 1015, at(5, 4)));
  feed(hidden_trade(25, at(5, 5)));

  EXPECT_EQ(symbol().added, 460);
  EXPECT_EQ(symbol().executed, 60);
  EXPECT_EQ(symbol().cancelled, 40);
  EXPECT_EQ(symbol().deleted, 100);
  EXPECT_EQ(symbol().replaced_out, 100);
  EXPECT_EQ(symbol().volume, 30 + 20 + 25);
  EXPECT_EQ(manager_->accounting_gap(1), 0);
  EXPECT_EQ(manager_->messages(), 15U);
}

TEST_F(BookManagerTest, AttributionIsKeptThroughReplace) {
  feed(add_mpid(1, 2, 'B', 100, 980, at(5, 0)));
  feed(replaced(2, 3, 100, 985, at(5, 1)));
  const itch::OrderStore& store = manager_->orders();
  EXPECT_EQ(store[store.find(3)].mpid, 0x56495254U);
}

TEST_F(BookManagerTest, CrossVolumeCountsOnceAndNonPrintableExecutionsDoNot) {
  feed(add(1, 1, 'B', 100, 990, at(9, 0)));
  feed(executed_price(1, 100, 'N', at(9, 30)));
  feed(cross('O', 100, at(9, 30)));
  EXPECT_EQ(symbol().volume, 100);
  EXPECT_EQ(symbol().cross_volume, 100);
}

TEST_F(BookManagerTest, DataQualityCounters) {
  feed(add(1, 1, 'B', 100, 990, at(5, 0)));
  feed(add(1, 1, 'B', 100, 990, at(5, 0)));
  EXPECT_EQ(counters().duplicate_adds, 1U);

  feed(executed(9, 1, at(5, 0)));
  feed(executed_price(9, 1, 'Y', at(5, 0)));
  feed(cancelled(9, 1, at(5, 0)));
  feed(deleted(9, at(5, 0)));
  feed(replaced(9, 10, 1, 1, at(5, 0)));
  EXPECT_EQ(counters().orphans, 5U);

  feed(executed(1, 150, at(5, 0)));
  EXPECT_EQ(counters().over_executions, 1U);
  EXPECT_EQ(symbol().executed, 100);

  feed(add(2, 20, 'B', 10, 500, at(5, 0)));
  EXPECT_EQ(counters().undefined_locates, 1U);

  feed(add(1, 30, 'B', 10, 500, at(5, 0)));
  feed(add(1, 31, 'B', 10, 500, at(5, 0)));
  feed(replaced(30, 31, 10, 500, at(5, 0)));
  EXPECT_EQ(counters().duplicate_adds, 2U);
  EXPECT_EQ(manager_->accounting_gap(1), 0);

  feed(system_event('Q', at(4, 0)));
  EXPECT_EQ(counters().timestamp_regressions, 1U);
}

TEST_F(BookManagerTest, CrossedInContinuousTradingIsAViolation) {
  feed(cross('O', 0, at(9, 30)));
  feed(add(1, 1, 'B', 100, 1000, at(10, 0)));
  feed(add(1, 2, 'S', 100, 990, at(10, 0)));
  EXPECT_EQ(counters().crossed_outside_auction, 1U);
  EXPECT_EQ(symbol().crossed_outside, 1);
  ASSERT_EQ(counters().crossed_sample_count, 1U);
  EXPECT_EQ(counters().crossed_samples[0].locate, 1);
  EXPECT_EQ(counters().crossed_samples[0].seq, 6U);
}

TEST_F(BookManagerTest, LockedBookIsReportedSeparately) {
  feed(cross('O', 0, at(9, 30)));
  feed(add(1, 1, 'B', 100, 1000, at(10, 0)));
  feed(add(1, 2, 'S', 100, 1000, at(10, 0)));
  EXPECT_EQ(counters().locked_outside_auction, 1U);
  EXPECT_EQ(symbol().locked_outside, 1);
  EXPECT_EQ(counters().crossed_outside_auction, 0U);
}

TEST_F(BookManagerTest, CrossedInsideTheOpeningWindowIsAllowed) {
  feed(noii('O', at(9, 25)));
  feed(add(1, 1, 'B', 100, 1000, at(9, 26)));
  feed(add(1, 2, 'S', 100, 990, at(9, 26)));
  EXPECT_EQ(counters().crossed_in_auction, 1U);
  EXPECT_EQ(counters().crossed_outside_auction, 0U);

  feed(cross('O', 100, at(9, 30)));
  feed(deleted(1, at(9, 30)));
  feed(add(1, 3, 'B', 100, 1001, at(9, 31)));
  EXPECT_EQ(counters().crossed_outside_auction, 1U);
}

TEST_F(BookManagerTest, ScheduledWindowsApplyToNasdaqListedSymbols) {
  feed(add(1, 1, 'B', 100, 1000, at(9, 28)));
  feed(add(1, 2, 'S', 100, 990, at(9, 28)));
  EXPECT_EQ(counters().crossed_in_auction, 1U);
  feed(cross('O', 0, at(9, 30)));
  feed(deleted(1, at(9, 30)));
  feed(deleted(2, at(9, 30)));

  feed(add(1, 3, 'B', 100, 1000, at(15, 50)));
  feed(add(1, 4, 'S', 100, 990, at(15, 50)));
  EXPECT_EQ(counters().crossed_in_auction, 2U);
  EXPECT_EQ(counters().crossed_outside_auction, 0U);
}

TEST_F(BookManagerTest, NoScheduledWindowForOtherListings) {
  feed(directory(2, "NYSE", 'N'));
  feed(action(2, 'T', at(4, 0)));
  feed(add(2, 1, 'B', 100, 1000, at(9, 29)));
  feed(add(2, 2, 'S', 100, 990, at(9, 29)));
  EXPECT_EQ(counters().crossed_outside_auction, 1U);
}

TEST_F(BookManagerTest, ClosingNoiiOpensTheClosingWindow) {
  feed(cross('O', 0, at(9, 30)));
  feed(noii('C', at(15, 49)));
  feed(add(1, 1, 'B', 100, 1000, at(15, 49)));
  feed(add(1, 2, 'S', 100, 990, at(15, 49)));
  EXPECT_EQ(counters().crossed_in_auction, 1U);
  feed(cross('C', 100, at(16, 0)));
  feed(deleted(1, at(16, 0)));
  feed(add(1, 3, 'B', 100, 1001, at(16, 1)));
  EXPECT_EQ(counters().crossed_outside_auction, 1U);
}

TEST_F(BookManagerTest, HaltIsAWindowUntilResume) {
  feed(cross('O', 0, at(9, 30)));
  feed(action(1, 'H', at(11, 0)));
  feed(add(1, 1, 'B', 100, 1000, at(11, 1)));
  feed(add(1, 2, 'S', 100, 990, at(11, 1)));
  EXPECT_EQ(counters().crossed_in_auction, 1U);
  EXPECT_EQ(counters().crossed_outside_auction, 0U);

  feed(noii('H', at(11, 2)));
  feed(action(1, 'T', at(11, 3)));
  feed(add(1, 3, 'B', 100, 1001, at(11, 3)));
  EXPECT_EQ(counters().crossed_in_auction, 1U + 1U);
  feed(cross('H', 100, at(11, 4)));
  feed(deleted(1, at(11, 4)));
  feed(deleted(3, at(11, 4)));
  feed(add(1, 4, 'B', 100, 1002, at(11, 5)));
  EXPECT_EQ(counters().crossed_outside_auction, 1U);
}

TEST_F(BookManagerTest, ACrossedBookSettlesAfterTheCrossPrint) {
  // Real order on a halt cross: Q, then the resume, then the executions that unwind the cross,
  // with ordinary quotes and trades arriving among them.
  feed(cross('O', 0, at(9, 30)));
  feed(action(1, 'H', at(11, 0)));
  feed(add(1, 1, 'B', 100, 1000, at(11, 1)));
  feed(add(1, 2, 'S', 100, 990, at(11, 1)));
  feed(add(1, 3, 'S', 100, 995, at(11, 1)));
  const std::uint64_t printed = at(11, 5);
  feed(cross('H', 100, printed));
  feed(action(1, 'T', printed));

  feed(executed_price(2, 50, 'N', printed + 100));
  feed(add(1, 4, 'B', 10, 900, printed + 200));
  feed(executed(2, 1, printed + 300));
  EXPECT_EQ(counters().crossed_outside_auction, 0U);
  EXPECT_EQ(counters().crossed_in_settlement, 3U);
  EXPECT_EQ(counters().longest_settlement_ns, 300U);
  EXPECT_EQ(counters().longest_settlement_locate, 1);

  // The book uncrosses, which ends settlement.
  feed(executed_price(1, 100, 'N', printed + 400));
  EXPECT_FALSE(manager_->book(1).touching());
  feed(add(1, 5, 'B', 100, 1000, printed + 500));
  EXPECT_EQ(counters().crossed_outside_auction, 1U);
}

TEST_F(BookManagerTest, ACrossPrintOnAnOpenBookStartsNoSettlement) {
  feed(cross('O', 0, at(9, 30)));
  feed(add(1, 1, 'B', 100, 1000, at(10, 0)));
  feed(add(1, 2, 'S', 100, 1010, at(10, 0)));
  feed(cross('H', 0, at(10, 1)));
  feed(add(1, 3, 'B', 100, 1020, at(10, 2)));
  EXPECT_EQ(counters().crossed_outside_auction, 1U);
  EXPECT_EQ(counters().crossed_in_settlement, 0U);
}

TEST_F(BookManagerTest, LockedDuringSettlementIsCountedApart) {
  feed(cross('O', 0, at(9, 30)));
  feed(add(1, 1, 'B', 100, 1000, at(11, 0)));
  feed(add(1, 2, 'S', 100, 1000, at(11, 0)));
  feed(cross('H', 0, at(11, 1)));
  feed(add(1, 3, 'S', 100, 1010, at(11, 1)));
  EXPECT_EQ(counters().locked_in_settlement, 1U);
}

TEST_F(BookManagerTest, BrokenTradesAreRemembered) {
  feed(broken(77, at(16, 0)));
  EXPECT_EQ(counters().broken_trades, 1U);
  EXPECT_EQ(manager_->broken_matches(), std::vector<std::uint64_t>{77});
}

TEST_F(BookManagerTest, StateMessagesUpdateTradingState) {
  MessageBytes reg('Y', 1, 0, at(5, 0));
  reg.text("SYM", 8).alpha('1');
  feed(reg);
  MessageBytes halt('h', 1, 0, at(5, 0));
  halt.text("SYM", 8).alpha('Q').alpha('H');
  feed(halt);
  feed(noii('C', at(15, 50)));

  const itch::SymbolState& state = manager_->state()[1];
  EXPECT_EQ(state.reg_sho, '1');
  EXPECT_EQ(state.operational_halt, 'H');
  EXPECT_TRUE(state.noii.valid);
  EXPECT_EQ(state.noii.cross_type, 'C');
  EXPECT_EQ(state.noii.near_price, 100U);
  EXPECT_EQ(state.noii.imbalance_shares, 5U);
}

TEST_F(BookManagerTest, RecordOnlyMessagesCountTowardSeq) {
  const std::uint64_t before = manager_->messages();
  MessageBytes position('L', 1, 0, at(5, 0));
  position.text("GSCO", 4).text("SYM", 8).alpha('Y').alpha('N').alpha('A');
  feed(position);
  MessageBytes decline('V', 0, 0, at(5, 0));
  decline.u64(1).u64(2).u64(3);
  feed(decline);
  MessageBytes status('W', 0, 0, at(5, 0));
  status.alpha('1');
  feed(status);
  MessageBytes ipo('K', 1, 0, at(5, 0));
  ipo.text("SYM", 8).u32(1).alpha('A').u32(2);
  feed(ipo);
  MessageBytes collar('J', 1, 0, at(5, 0));
  collar.text("SYM", 8).u32(1).u32(2).u32(3).u32(4);
  feed(collar);
  MessageBytes rpii('N', 1, 0, at(5, 0));
  rpii.text("SYM", 8).alpha('A');
  feed(rpii);
  MessageBytes dlcr('O', 1, 0, at(5, 0));
  dlcr.text("SYM", 8).alpha('Y').u32(1).u32(2).u32(3).u64(4).u32(5).u32(6);
  feed(dlcr);
  EXPECT_EQ(manager_->messages(), before + 7);
}

TEST_F(BookManagerTest, DigestDependsOnOrderOfTheQueue) {
  BookManager other(itch::BookConfig{.order_capacity_hint = 64});
  const auto replay = [](BookManager& target, std::uint64_t first, std::uint64_t second) {
    const auto dir = directory(1, "SYM", 'Q');
    ASSERT_EQ(itch::parse(dir.bytes(), target), itch::ParseStatus::Ok);
    const auto a = add(1, first, 'B', 100, 1000, at(5, 0));
    const auto b = add(1, second, 'B', 100, 1000, at(5, 0));
    ASSERT_EQ(itch::parse(a.bytes(), target), itch::ParseStatus::Ok);
    ASSERT_EQ(itch::parse(b.bytes(), target), itch::ParseStatus::Ok);
  };
  BookManager same(itch::BookConfig{.order_capacity_hint = 64});
  BookManager swapped(itch::BookConfig{.order_map = itch::OrderMapKind::Std});
  replay(other, 1, 2);
  replay(same, 1, 2);
  replay(swapped, 2, 1);
  EXPECT_EQ(other.digest(), same.digest());
  EXPECT_NE(other.digest(), swapped.digest());
}

TEST_F(BookManagerTest, ResetReturnsToAFreshDay) {
  feed(add(1, 1, 'B', 100, 1000, at(5, 0)));
  manager_->reset();
  EXPECT_EQ(manager_->messages(), 0U);
  EXPECT_EQ(manager_->orders().live(), 0U);
  EXPECT_FALSE(manager_->instruments().defined(1));
  BookManager fresh(itch::BookConfig{.order_capacity_hint = 64});
  EXPECT_EQ(manager_->digest(), fresh.digest());
}

TEST(BookManagerSide, SellOrdersRestOnTheAsk) {
  BookManager manager(itch::BookConfig{.order_capacity_hint = 64});
  const auto dir = directory(1, "SYM", 'Q');
  const auto order = add(1, 1, 'S', 100, 1000, 0);
  ASSERT_EQ(itch::parse(dir.bytes(), manager), itch::ParseStatus::Ok);
  ASSERT_EQ(itch::parse(order.bytes(), manager), itch::ParseStatus::Ok);
  EXPECT_EQ(manager.book(1).best(Side::Ask), (Level{1000, 100, 1}));
  EXPECT_FALSE(manager.book(1).best(Side::Bid));
}

}  // namespace
