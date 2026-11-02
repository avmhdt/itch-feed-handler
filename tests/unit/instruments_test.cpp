#include "itch/state/instruments.hpp"

#include <gtest/gtest.h>

#include "message_bytes.hpp"

namespace {

itch::test::MessageBytes directory(std::uint16_t locate, std::string_view symbol) {
  itch::test::MessageBytes m('R', locate, 0, 0);
  m.text(symbol, 8)
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
      .alpha('Y')
      .u32(1)
      .alpha('N');
  return m;
}

TEST(LocateTable, DefinesAndFinds) {
  itch::LocateTable table;
  EXPECT_FALSE(table.defined(7));
  const auto message = directory(7, "AAPL");
  table.define(itch::StockDirectory{itch::Header{message.bytes().data()}});

  EXPECT_TRUE(table.defined(7));
  EXPECT_EQ(table[7].symbol(), "AAPL");
  EXPECT_EQ(table[7].market_category, 'Q');
  EXPECT_EQ(table[7].round_lot_size, 100U);
  EXPECT_EQ(table[7].etp_flag, 'Y');
  EXPECT_EQ(table.find("AAPL"), 7);
  EXPECT_EQ(table.find("MSFT"), 0);
}

TEST(LocateTable, AllIsInLocateOrder) {
  itch::LocateTable table;
  const auto late = directory(900, "ZZZZ");
  const auto early = directory(3, "AAAA");
  table.define(itch::StockDirectory{itch::Header{late.bytes().data()}});
  table.define(itch::StockDirectory{itch::Header{early.bytes().data()}});
  const auto all = table.all();
  ASSERT_EQ(all.size(), 2U);
  EXPECT_EQ(all[0]->locate, 3);
  EXPECT_EQ(all[1]->locate, 900);
}

}  // namespace
