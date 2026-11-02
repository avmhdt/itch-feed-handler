#include "itch/proto/messages.hpp"

#include <gtest/gtest.h>

#include <cstddef>

#include "message_bytes.hpp"

namespace {

using itch::test::MessageBytes;

constexpr std::uint64_t kTs = 0x0102'0304'0506ULL;

template <class View>
View view_of(const MessageBytes& message) {
  EXPECT_EQ(message.bytes().size(), View::kLength);
  return View{itch::Header{message.bytes().data()}};
}

template <class View>
void expect_header(const View& view, std::uint16_t locate) {
  EXPECT_EQ(view.type(), View::kType);
  EXPECT_EQ(view.stock_locate(), locate);
  EXPECT_EQ(view.tracking_number(), 0x0A0B);
  EXPECT_EQ(view.timestamp_ns(), kTs);
}

TEST(Messages, TrimRight) {
  EXPECT_EQ(itch::trim_right("AAPL    "), "AAPL");
  EXPECT_EQ(itch::trim_right("        "), "");
  EXPECT_EQ(itch::trim_right("ABCDEFGH"), "ABCDEFGH");
}

TEST(Messages, SystemEvent) {
  MessageBytes m('S', 0, 0x0A0B, kTs);
  m.alpha('Q');
  const auto v = view_of<itch::SystemEvent>(m);
  expect_header(v, 0);
  EXPECT_EQ(v.event_code(), 'Q');
}

TEST(Messages, StockDirectory) {
  MessageBytes m('R', 7, 0x0A0B, kTs);
  m.text("AAPL", 8)
      .alpha('Q')
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
      .u32(3)
      .alpha('Y');
  const auto v = view_of<itch::StockDirectory>(m);
  expect_header(v, 7);
  EXPECT_EQ(itch::trim_right(v.stock()), "AAPL");
  EXPECT_EQ(v.market_category(), 'Q');
  EXPECT_EQ(v.financial_status(), 'N');
  EXPECT_EQ(v.round_lot_size(), 100U);
  EXPECT_EQ(v.round_lots_only(), 'N');
  EXPECT_EQ(v.issue_classification(), 'C');
  EXPECT_EQ(v.issue_subtype(), "Z ");
  EXPECT_EQ(v.authenticity(), 'P');
  EXPECT_EQ(v.short_sale_threshold(), 'N');
  EXPECT_EQ(v.ipo_flag(), 'N');
  EXPECT_EQ(v.luld_tier(), '1');
  EXPECT_EQ(v.etp_flag(), 'N');
  EXPECT_EQ(v.etp_leverage_factor(), 3U);
  EXPECT_EQ(v.inverse_indicator(), 'Y');
}

TEST(Messages, TradingAction) {
  MessageBytes m('H', 9, 0x0A0B, kTs);
  m.text("MSFT", 8).alpha('H').alpha(' ').text("LUDP", 4);
  const auto v = view_of<itch::TradingAction>(m);
  expect_header(v, 9);
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.trading_state(), 'H');
  EXPECT_EQ(v.reserved(), ' ');
  EXPECT_EQ(v.reason(), "LUDP");
}

TEST(Messages, RegSho) {
  MessageBytes m('Y', 9, 0x0A0B, kTs);
  m.text("MSFT", 8).alpha('1');
  const auto v = view_of<itch::RegSho>(m);
  expect_header(v, 9);
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.action(), '1');
}

TEST(Messages, ParticipantPosition) {
  MessageBytes m('L', 9, 0x0A0B, kTs);
  m.text("GSCO", 4).text("MSFT", 8).alpha('Y').alpha('N').alpha('A');
  const auto v = view_of<itch::ParticipantPosition>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.mpid(), "GSCO");
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.primary_market_maker(), 'Y');
  EXPECT_EQ(v.market_maker_mode(), 'N');
  EXPECT_EQ(v.participant_state(), 'A');
}

TEST(Messages, MwcbDecline) {
  MessageBytes m('V', 0, 0x0A0B, kTs);
  m.u64(0x1000000000ULL).u64(0x2000000000ULL).u64(0x3000000000ULL);
  const auto v = view_of<itch::MwcbDecline>(m);
  expect_header(v, 0);
  EXPECT_EQ(v.level1(), 0x1000000000ULL);
  EXPECT_EQ(v.level2(), 0x2000000000ULL);
  EXPECT_EQ(v.level3(), 0x3000000000ULL);
}

TEST(Messages, MwcbStatus) {
  MessageBytes m('W', 0, 0x0A0B, kTs);
  m.alpha('2');
  const auto v = view_of<itch::MwcbStatus>(m);
  expect_header(v, 0);
  EXPECT_EQ(v.breached_level(), '2');
}

TEST(Messages, IpoQuoting) {
  MessageBytes m('K', 0, 0x0A0B, kTs);
  m.text("NEWCO", 8).u32(36000).alpha('A').u32(210000);
  const auto v = view_of<itch::IpoQuoting>(m);
  expect_header(v, 0);
  EXPECT_EQ(itch::trim_right(v.stock()), "NEWCO");
  EXPECT_EQ(v.release_time_s(), 36000U);
  EXPECT_EQ(v.release_qualifier(), 'A');
  EXPECT_EQ(v.ipo_price(), 210000U);
}

TEST(Messages, LuldCollar) {
  MessageBytes m('J', 9, 0x0A0B, kTs);
  m.text("MSFT", 8).u32(1000000).u32(1100000).u32(900000).u32(2);
  const auto v = view_of<itch::LuldCollar>(m);
  expect_header(v, 9);
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.reference_price(), 1000000U);
  EXPECT_EQ(v.upper_price(), 1100000U);
  EXPECT_EQ(v.lower_price(), 900000U);
  EXPECT_EQ(v.extension(), 2U);
}

TEST(Messages, OperationalHalt) {
  MessageBytes m('h', 9, 0x0A0B, kTs);
  m.text("MSFT", 8).alpha('Q').alpha('H');
  const auto v = view_of<itch::OperationalHalt>(m);
  expect_header(v, 9);
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.market_code(), 'Q');
  EXPECT_EQ(v.action(), 'H');
}

TEST(Messages, AddOrder) {
  MessageBytes m('A', 9, 0x0A0B, kTs);
  m.u64(0x1122334455667788ULL).alpha('B').u32(500).text("MSFT", 8).u32(1654300);
  const auto v = view_of<itch::AddOrder>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.order_ref(), 0x1122334455667788ULL);
  EXPECT_EQ(v.side(), 'B');
  EXPECT_EQ(v.shares(), 500U);
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.price(), 1654300U);
}

TEST(Messages, AddOrderMpid) {
  MessageBytes m('F', 9, 0x0A0B, kTs);
  m.u64(42).alpha('S').u32(200).text("MSFT", 8).u32(1654400).text("VIRT", 4);
  const auto v = view_of<itch::AddOrderMpid>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.order_ref(), 42U);
  EXPECT_EQ(v.side(), 'S');
  EXPECT_EQ(v.shares(), 200U);
  EXPECT_EQ(v.price(), 1654400U);
  EXPECT_EQ(v.attribution(), "VIRT");
}

TEST(Messages, OrderExecuted) {
  MessageBytes m('E', 9, 0x0A0B, kTs);
  m.u64(42).u32(100).u64(9001);
  const auto v = view_of<itch::OrderExecuted>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.order_ref(), 42U);
  EXPECT_EQ(v.executed_shares(), 100U);
  EXPECT_EQ(v.match_number(), 9001U);
}

TEST(Messages, OrderExecutedPrice) {
  MessageBytes m('C', 9, 0x0A0B, kTs);
  m.u64(42).u32(100).u64(9002).alpha('N').u32(1654350);
  const auto v = view_of<itch::OrderExecutedPrice>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.order_ref(), 42U);
  EXPECT_EQ(v.executed_shares(), 100U);
  EXPECT_EQ(v.match_number(), 9002U);
  EXPECT_EQ(v.printable(), 'N');
  EXPECT_EQ(v.execution_price(), 1654350U);
}

TEST(Messages, OrderCancel) {
  MessageBytes m('X', 9, 0x0A0B, kTs);
  m.u64(42).u32(50);
  const auto v = view_of<itch::OrderCancel>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.order_ref(), 42U);
  EXPECT_EQ(v.cancelled_shares(), 50U);
}

TEST(Messages, OrderDelete) {
  MessageBytes m('D', 9, 0x0A0B, kTs);
  m.u64(42);
  const auto v = view_of<itch::OrderDelete>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.order_ref(), 42U);
}

TEST(Messages, OrderReplace) {
  MessageBytes m('U', 9, 0x0A0B, kTs);
  m.u64(42).u64(43).u32(300).u32(1654500);
  const auto v = view_of<itch::OrderReplace>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.original_ref(), 42U);
  EXPECT_EQ(v.new_ref(), 43U);
  EXPECT_EQ(v.shares(), 300U);
  EXPECT_EQ(v.price(), 1654500U);
}

TEST(Messages, Trade) {
  MessageBytes m('P', 9, 0x0A0B, kTs);
  m.u64(0).alpha('B').u32(100).text("MSFT", 8).u32(1654300).u64(9003);
  const auto v = view_of<itch::Trade>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.order_ref(), 0U);
  EXPECT_EQ(v.side(), 'B');
  EXPECT_EQ(v.shares(), 100U);
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.price(), 1654300U);
  EXPECT_EQ(v.match_number(), 9003U);
}

TEST(Messages, CrossTrade) {
  MessageBytes m('Q', 9, 0x0A0B, kTs);
  m.u64(1'000'000).text("MSFT", 8).u32(1654300).u64(9004).alpha('C');
  const auto v = view_of<itch::CrossTrade>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.shares(), 1'000'000U);
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.cross_price(), 1654300U);
  EXPECT_EQ(v.match_number(), 9004U);
  EXPECT_EQ(v.cross_type(), 'C');
}

TEST(Messages, BrokenTrade) {
  MessageBytes m('B', 9, 0x0A0B, kTs);
  m.u64(9003);
  const auto v = view_of<itch::BrokenTrade>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.match_number(), 9003U);
}

TEST(Messages, Noii) {
  MessageBytes m('I', 9, 0x0A0B, kTs);
  m.u64(5000)
      .u64(700)
      .alpha('B')
      .text("MSFT", 8)
      .u32(1654000)
      .u32(1654100)
      .u32(1654200)
      .alpha('C')
      .alpha('L');
  const auto v = view_of<itch::Noii>(m);
  expect_header(v, 9);
  EXPECT_EQ(v.paired_shares(), 5000U);
  EXPECT_EQ(v.imbalance_shares(), 700U);
  EXPECT_EQ(v.imbalance_direction(), 'B');
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.far_price(), 1654000U);
  EXPECT_EQ(v.near_price(), 1654100U);
  EXPECT_EQ(v.reference_price(), 1654200U);
  EXPECT_EQ(v.cross_type(), 'C');
  EXPECT_EQ(v.price_variation(), 'L');
}

TEST(Messages, Rpii) {
  MessageBytes m('N', 9, 0x0A0B, kTs);
  m.text("MSFT", 8).alpha('A');
  const auto v = view_of<itch::Rpii>(m);
  expect_header(v, 9);
  EXPECT_EQ(itch::trim_right(v.stock()), "MSFT");
  EXPECT_EQ(v.interest_flag(), 'A');
}

TEST(Messages, Dlcr) {
  MessageBytes m('O', 9, 0x0A0B, kTs);
  m.text("NEWCO", 8)
      .alpha('Y')
      .u32(100000)
      .u32(200000)
      .u32(150000)
      .u64(0x0102030405060708ULL)
      .u32(135000)
      .u32(165000);
  const auto v = view_of<itch::Dlcr>(m);
  expect_header(v, 9);
  EXPECT_EQ(itch::trim_right(v.stock()), "NEWCO");
  EXPECT_EQ(v.open_eligibility(), 'Y');
  EXPECT_EQ(v.minimum_allowable_price(), 100000U);
  EXPECT_EQ(v.maximum_allowable_price(), 200000U);
  EXPECT_EQ(v.near_execution_price(), 150000U);
  EXPECT_EQ(v.near_execution_time(), 0x0102030405060708ULL);
  EXPECT_EQ(v.lower_price_collar(), 135000U);
  EXPECT_EQ(v.upper_price_collar(), 165000U);
}

}  // namespace
