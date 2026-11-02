#pragma once

#include <array>
#include <cstdint>

namespace itch {

__extension__ using UInt128 = unsigned __int128;

/// Fixed-size output records, handed from the hot thread to the writer thread over SPSC queues.
/// They carry integers only; floats appear when a column is written (DQ-07). Arrow stays out of
/// this header, so the rest of the handler does not depend on it (R-07).

/// One row of book_l3. `event` is the first letter of add, exec, cancel, delete or replace.
struct L3Record {
  std::int64_t ts = 0;
  std::int64_t seq = 0;
  std::int64_t price_ticks = 0;
  std::int64_t qty = 0;
  std::uint64_t order_id = 0;
  std::uint64_t new_order_id = 0;  // Only for replace.
  std::uint64_t match_id = 0;      // Only for exec.
  std::uint32_t mpid = 0;          // Packed attribution, 0 for none.
  std::uint16_t tracking_number = 0;
  std::uint16_t locate = 0;
  std::int8_t side = 0;
  char event = 'a';
  char msg_type = 'A';
  char printable = 0;  // 'Y', 'N', or 0 for null.
};

/// One row of trades. `broken` is set by the end-of-day correction pass.
struct TradeRecord {
  std::int64_t ts = 0;
  std::int64_t seq = 0;
  std::int64_t price_ticks = 0;
  std::int64_t qty = 0;
  std::uint64_t match_id = 0;
  std::uint16_t locate = 0;
  std::int8_t side = 0;
  char source = 'E';
  char cross_type = 0;  // 0 for null.
  bool printable = true;
  bool broken = false;
};

/// One row of bbo. A price of 0 means the side is empty.
struct BboRecord {
  std::int64_t ts = 0;
  std::int64_t seq = 0;
  std::int64_t bid_ticks = 0;
  std::int64_t ask_ticks = 0;
  std::int64_t bid_qty = 0;
  std::int64_t ask_qty = 0;
  std::int32_t bid_orders = 0;
  std::int32_t ask_orders = 0;
  std::uint16_t locate = 0;
};

/// One row of book_snapshots.
struct SnapshotRecord {
  std::int64_t ts = 0;
  std::int64_t price_ticks = 0;
  std::int64_t qty = 0;
  std::int32_t order_count = 0;
  std::uint16_t level = 0;
  std::uint16_t locate = 0;
  std::int8_t side = 0;
};

/// One row of trading_state; locate 0 is the market-wide partition.
struct StateRecord {
  std::int64_t ts = 0;
  std::int64_t seq = 0;
  std::uint16_t locate = 0;
  char msg_type = 'S';
  char state = ' ';
  std::array<char, 4> reason{};  // All zero for null.
  std::array<char, 120> details{};
};

/// One row of noii.
struct NoiiRecord {
  std::int64_t ts = 0;
  std::int64_t seq = 0;
  std::int64_t paired_shares = 0;
  std::int64_t imbalance_shares = 0;
  std::int64_t far_price_ticks = 0;
  std::int64_t near_price_ticks = 0;
  std::int64_t ref_price_ticks = 0;
  std::uint16_t locate = 0;
  char msg_type = 'I';
  char direction = ' ';
  char cross_type = ' ';
  char price_variation = ' ';
  std::array<char, 120> details{};
};

/// One row of bars. Notional is exact: ticks times shares in 128 bits (NFR-05).
struct BarRecord {
  std::int64_t ts_start = 0;
  std::int64_t ts_end = 0;
  std::int64_t open_ticks = 0;
  std::int64_t high_ticks = 0;
  std::int64_t low_ticks = 0;
  std::int64_t close_ticks = 0;
  std::int64_t volume = 0;
  std::int64_t trade_count = 0;
  std::int64_t cross_volume = 0;
  UInt128 notional_tick_shares = 0;
  std::uint16_t locate = 0;
};

}  // namespace itch
