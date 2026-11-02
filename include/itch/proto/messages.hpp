#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "itch/proto/endian.hpp"
#include "itch/proto/spec_table.hpp"

namespace itch {

/// Removes the right-hand space padding of an ITCH alpha field.
[[nodiscard]] constexpr std::string_view trim_right(std::string_view text) noexcept {
  const std::size_t end = text.find_last_not_of(' ');
  return end == std::string_view::npos ? std::string_view{} : text.substr(0, end + 1);
}

/// Non-owning view of the fields shared by every message: type (1), stock locate (2), tracking
/// number (2) and timestamp (6, nanoseconds since midnight). Views hold a pointer into the frame
/// and decode on access, so a view is only valid while the frame is. ITCH 5.0 "Message Formats".
struct Header {
  const std::byte* p;

  [[nodiscard]] MsgType type() const noexcept { return static_cast<MsgType>(alpha(0)); }
  [[nodiscard]] std::uint16_t stock_locate() const noexcept { return u16(1); }
  [[nodiscard]] std::uint16_t tracking_number() const noexcept { return u16(3); }
  [[nodiscard]] std::uint64_t timestamp_ns() const noexcept { return load_be48(p + 5); }

 protected:
  [[nodiscard]] std::uint16_t u16(std::size_t offset) const noexcept {
    return load_be<std::uint16_t>(p + offset);
  }
  [[nodiscard]] std::uint32_t u32(std::size_t offset) const noexcept {
    return load_be<std::uint32_t>(p + offset);
  }
  [[nodiscard]] std::uint64_t u64(std::size_t offset) const noexcept {
    return load_be<std::uint64_t>(p + offset);
  }
  [[nodiscard]] char alpha(std::size_t offset) const noexcept {
    return std::to_integer<char>(p[offset]);
  }
  [[nodiscard]] std::string_view text(std::size_t offset, std::size_t n) const noexcept {
    return as_text(p + offset, n);
  }
};

/// 1.1 System Event.
struct SystemEvent : Header {
  static constexpr MsgType kType = MsgType::SystemEvent;
  static constexpr std::size_t kLength = 12;
  [[nodiscard]] char event_code() const noexcept { return alpha(11); }
};

/// 1.2.1 Stock Directory.
struct StockDirectory : Header {
  static constexpr MsgType kType = MsgType::StockDirectory;
  static constexpr std::size_t kLength = 39;
  [[nodiscard]] std::string_view stock() const noexcept { return text(11, 8); }
  [[nodiscard]] char market_category() const noexcept { return alpha(19); }
  [[nodiscard]] char financial_status() const noexcept { return alpha(20); }
  [[nodiscard]] std::uint32_t round_lot_size() const noexcept { return u32(21); }
  [[nodiscard]] char round_lots_only() const noexcept { return alpha(25); }
  [[nodiscard]] char issue_classification() const noexcept { return alpha(26); }
  [[nodiscard]] std::string_view issue_subtype() const noexcept { return text(27, 2); }
  [[nodiscard]] char authenticity() const noexcept { return alpha(29); }
  [[nodiscard]] char short_sale_threshold() const noexcept { return alpha(30); }
  [[nodiscard]] char ipo_flag() const noexcept { return alpha(31); }
  [[nodiscard]] char luld_tier() const noexcept { return alpha(32); }
  [[nodiscard]] char etp_flag() const noexcept { return alpha(33); }
  [[nodiscard]] std::uint32_t etp_leverage_factor() const noexcept { return u32(34); }
  [[nodiscard]] char inverse_indicator() const noexcept { return alpha(38); }
};

/// 1.2.2 Stock Trading Action.
struct TradingAction : Header {
  static constexpr MsgType kType = MsgType::TradingAction;
  static constexpr std::size_t kLength = 25;
  [[nodiscard]] std::string_view stock() const noexcept { return text(11, 8); }
  [[nodiscard]] char trading_state() const noexcept { return alpha(19); }
  [[nodiscard]] char reserved() const noexcept { return alpha(20); }
  [[nodiscard]] std::string_view reason() const noexcept { return text(21, 4); }
};

/// 1.2.3 Reg SHO Short Sale Price Test Restricted Indicator.
struct RegSho : Header {
  static constexpr MsgType kType = MsgType::RegSho;
  static constexpr std::size_t kLength = 20;
  [[nodiscard]] std::string_view stock() const noexcept { return text(11, 8); }
  [[nodiscard]] char action() const noexcept { return alpha(19); }
};

/// 1.2.4 Market Participant Position.
struct ParticipantPosition : Header {
  static constexpr MsgType kType = MsgType::ParticipantPosition;
  static constexpr std::size_t kLength = 26;
  [[nodiscard]] std::string_view mpid() const noexcept { return text(11, 4); }
  [[nodiscard]] std::string_view stock() const noexcept { return text(15, 8); }
  [[nodiscard]] char primary_market_maker() const noexcept { return alpha(23); }
  [[nodiscard]] char market_maker_mode() const noexcept { return alpha(24); }
  [[nodiscard]] char participant_state() const noexcept { return alpha(25); }
};

/// 1.2.5.1 MWCB Decline Level. Levels are Price(8), eight implied decimals.
struct MwcbDecline : Header {
  static constexpr MsgType kType = MsgType::MwcbDecline;
  static constexpr std::size_t kLength = 35;
  [[nodiscard]] std::uint64_t level1() const noexcept { return u64(11); }
  [[nodiscard]] std::uint64_t level2() const noexcept { return u64(19); }
  [[nodiscard]] std::uint64_t level3() const noexcept { return u64(27); }
};

/// 1.2.5.2 MWCB Status.
struct MwcbStatus : Header {
  static constexpr MsgType kType = MsgType::MwcbStatus;
  static constexpr std::size_t kLength = 12;
  [[nodiscard]] char breached_level() const noexcept { return alpha(11); }
};

/// 1.2.6 IPO Quoting Period Update.
struct IpoQuoting : Header {
  static constexpr MsgType kType = MsgType::IpoQuoting;
  static constexpr std::size_t kLength = 28;
  [[nodiscard]] std::string_view stock() const noexcept { return text(11, 8); }
  [[nodiscard]] std::uint32_t release_time_s() const noexcept { return u32(19); }
  [[nodiscard]] char release_qualifier() const noexcept { return alpha(23); }
  [[nodiscard]] std::uint32_t ipo_price() const noexcept { return u32(24); }
};

/// 1.2.7 Limit Up - Limit Down Auction Collar.
struct LuldCollar : Header {
  static constexpr MsgType kType = MsgType::LuldCollar;
  static constexpr std::size_t kLength = 35;
  [[nodiscard]] std::string_view stock() const noexcept { return text(11, 8); }
  [[nodiscard]] std::uint32_t reference_price() const noexcept { return u32(19); }
  [[nodiscard]] std::uint32_t upper_price() const noexcept { return u32(23); }
  [[nodiscard]] std::uint32_t lower_price() const noexcept { return u32(27); }
  [[nodiscard]] std::uint32_t extension() const noexcept { return u32(31); }
};

/// 1.2.8 Operational Halt.
struct OperationalHalt : Header {
  static constexpr MsgType kType = MsgType::OperationalHalt;
  static constexpr std::size_t kLength = 21;
  [[nodiscard]] std::string_view stock() const noexcept { return text(11, 8); }
  [[nodiscard]] char market_code() const noexcept { return alpha(19); }
  [[nodiscard]] char action() const noexcept { return alpha(20); }
};

/// 1.3.1 Add Order, no MPID attribution.
struct AddOrder : Header {
  static constexpr MsgType kType = MsgType::AddOrder;
  static constexpr std::size_t kLength = 36;
  [[nodiscard]] std::uint64_t order_ref() const noexcept { return u64(11); }
  [[nodiscard]] char side() const noexcept { return alpha(19); }
  [[nodiscard]] std::uint32_t shares() const noexcept { return u32(20); }
  [[nodiscard]] std::string_view stock() const noexcept { return text(24, 8); }
  [[nodiscard]] std::uint32_t price() const noexcept { return u32(32); }
};

/// 1.3.2 Add Order with MPID attribution: the 1.3.1 layout plus four attribution bytes.
struct AddOrderMpid : AddOrder {
  static constexpr MsgType kType = MsgType::AddOrderMpid;
  static constexpr std::size_t kLength = 40;
  [[nodiscard]] std::string_view attribution() const noexcept { return text(36, 4); }
};

/// 1.4.1 Order Executed.
struct OrderExecuted : Header {
  static constexpr MsgType kType = MsgType::OrderExecuted;
  static constexpr std::size_t kLength = 31;
  [[nodiscard]] std::uint64_t order_ref() const noexcept { return u64(11); }
  [[nodiscard]] std::uint32_t executed_shares() const noexcept { return u32(19); }
  [[nodiscard]] std::uint64_t match_number() const noexcept { return u64(23); }
};

/// 1.4.2 Order Executed With Price.
struct OrderExecutedPrice : Header {
  static constexpr MsgType kType = MsgType::OrderExecutedPrice;
  static constexpr std::size_t kLength = 36;
  [[nodiscard]] std::uint64_t order_ref() const noexcept { return u64(11); }
  [[nodiscard]] std::uint32_t executed_shares() const noexcept { return u32(19); }
  [[nodiscard]] std::uint64_t match_number() const noexcept { return u64(23); }
  [[nodiscard]] char printable() const noexcept { return alpha(31); }
  [[nodiscard]] std::uint32_t execution_price() const noexcept { return u32(32); }
};

/// 1.4.3 Order Cancel: a partial cancel.
struct OrderCancel : Header {
  static constexpr MsgType kType = MsgType::OrderCancel;
  static constexpr std::size_t kLength = 23;
  [[nodiscard]] std::uint64_t order_ref() const noexcept { return u64(11); }
  [[nodiscard]] std::uint32_t cancelled_shares() const noexcept { return u32(19); }
};

/// 1.4.4 Order Delete.
struct OrderDelete : Header {
  static constexpr MsgType kType = MsgType::OrderDelete;
  static constexpr std::size_t kLength = 19;
  [[nodiscard]] std::uint64_t order_ref() const noexcept { return u64(11); }
};

/// 1.4.5 Order Replace. Side, stock and attribution carry over from the original order.
struct OrderReplace : Header {
  static constexpr MsgType kType = MsgType::OrderReplace;
  static constexpr std::size_t kLength = 35;
  [[nodiscard]] std::uint64_t original_ref() const noexcept { return u64(11); }
  [[nodiscard]] std::uint64_t new_ref() const noexcept { return u64(19); }
  [[nodiscard]] std::uint32_t shares() const noexcept { return u32(27); }
  [[nodiscard]] std::uint32_t price() const noexcept { return u32(31); }
};

/// 1.5.1 Trade (non-cross). The reference is always zero and, since 2014-07-14, the side always
/// "B", so neither says anything about the match.
struct Trade : Header {
  static constexpr MsgType kType = MsgType::Trade;
  static constexpr std::size_t kLength = 44;
  [[nodiscard]] std::uint64_t order_ref() const noexcept { return u64(11); }
  [[nodiscard]] char side() const noexcept { return alpha(19); }
  [[nodiscard]] std::uint32_t shares() const noexcept { return u32(20); }
  [[nodiscard]] std::string_view stock() const noexcept { return text(24, 8); }
  [[nodiscard]] std::uint32_t price() const noexcept { return u32(32); }
  [[nodiscard]] std::uint64_t match_number() const noexcept { return u64(36); }
};

/// 1.5.2 Cross Trade.
struct CrossTrade : Header {
  static constexpr MsgType kType = MsgType::CrossTrade;
  static constexpr std::size_t kLength = 40;
  [[nodiscard]] std::uint64_t shares() const noexcept { return u64(11); }
  [[nodiscard]] std::string_view stock() const noexcept { return text(19, 8); }
  [[nodiscard]] std::uint32_t cross_price() const noexcept { return u32(27); }
  [[nodiscard]] std::uint64_t match_number() const noexcept { return u64(31); }
  [[nodiscard]] char cross_type() const noexcept { return alpha(39); }
};

/// 1.5.3 Broken Trade.
struct BrokenTrade : Header {
  static constexpr MsgType kType = MsgType::BrokenTrade;
  static constexpr std::size_t kLength = 19;
  [[nodiscard]] std::uint64_t match_number() const noexcept { return u64(11); }
};

/// 1.6 Net Order Imbalance Indicator.
struct Noii : Header {
  static constexpr MsgType kType = MsgType::Noii;
  static constexpr std::size_t kLength = 50;
  [[nodiscard]] std::uint64_t paired_shares() const noexcept { return u64(11); }
  [[nodiscard]] std::uint64_t imbalance_shares() const noexcept { return u64(19); }
  [[nodiscard]] char imbalance_direction() const noexcept { return alpha(27); }
  [[nodiscard]] std::string_view stock() const noexcept { return text(28, 8); }
  [[nodiscard]] std::uint32_t far_price() const noexcept { return u32(36); }
  [[nodiscard]] std::uint32_t near_price() const noexcept { return u32(40); }
  [[nodiscard]] std::uint32_t reference_price() const noexcept { return u32(44); }
  [[nodiscard]] char cross_type() const noexcept { return alpha(48); }
  [[nodiscard]] char price_variation() const noexcept { return alpha(49); }
};

/// 1.7 Retail Price Improvement Indicator.
struct Rpii : Header {
  static constexpr MsgType kType = MsgType::Rpii;
  static constexpr std::size_t kLength = 20;
  [[nodiscard]] std::string_view stock() const noexcept { return text(11, 8); }
  [[nodiscard]] char interest_flag() const noexcept { return alpha(19); }
};

/// 1.8 Direct Listing with Capital Raise Price Discovery. Absent from 2019 files.
struct Dlcr : Header {
  static constexpr MsgType kType = MsgType::Dlcr;
  static constexpr std::size_t kLength = 48;
  [[nodiscard]] std::string_view stock() const noexcept { return text(11, 8); }
  [[nodiscard]] char open_eligibility() const noexcept { return alpha(19); }
  [[nodiscard]] std::uint32_t minimum_allowable_price() const noexcept { return u32(20); }
  [[nodiscard]] std::uint32_t maximum_allowable_price() const noexcept { return u32(24); }
  [[nodiscard]] std::uint32_t near_execution_price() const noexcept { return u32(28); }
  [[nodiscard]] std::uint64_t near_execution_time() const noexcept { return u64(32); }
  [[nodiscard]] std::uint32_t lower_price_collar() const noexcept { return u32(40); }
  [[nodiscard]] std::uint32_t upper_price_collar() const noexcept { return u32(44); }
};

template <class View>
inline constexpr bool kViewMatchesTable = View::kLength == message_length(View::kType);

static_assert(kViewMatchesTable<SystemEvent>);
static_assert(kViewMatchesTable<StockDirectory>);
static_assert(kViewMatchesTable<TradingAction>);
static_assert(kViewMatchesTable<RegSho>);
static_assert(kViewMatchesTable<ParticipantPosition>);
static_assert(kViewMatchesTable<MwcbDecline>);
static_assert(kViewMatchesTable<MwcbStatus>);
static_assert(kViewMatchesTable<IpoQuoting>);
static_assert(kViewMatchesTable<LuldCollar>);
static_assert(kViewMatchesTable<OperationalHalt>);
static_assert(kViewMatchesTable<AddOrder>);
static_assert(kViewMatchesTable<AddOrderMpid>);
static_assert(kViewMatchesTable<OrderExecuted>);
static_assert(kViewMatchesTable<OrderExecutedPrice>);
static_assert(kViewMatchesTable<OrderCancel>);
static_assert(kViewMatchesTable<OrderDelete>);
static_assert(kViewMatchesTable<OrderReplace>);
static_assert(kViewMatchesTable<Trade>);
static_assert(kViewMatchesTable<CrossTrade>);
static_assert(kViewMatchesTable<BrokenTrade>);
static_assert(kViewMatchesTable<Noii>);
static_assert(kViewMatchesTable<Rpii>);
static_assert(kViewMatchesTable<Dlcr>);

}  // namespace itch
