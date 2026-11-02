// FR-27: arbitrary bytes into the frame reader and parser, in lenient mode. Each accessor is read
// so that ASan and UBSan check every field offset of every view.

#include <cstddef>
#include <cstdint>
#include <span>

#include "itch/io/file_driver.hpp"

namespace {

class Touch {
 public:
  void on(const itch::SystemEvent& m) { mix(m, m.event_code()); }
  void on(const itch::StockDirectory& m) {
    mix(m, m.stock().size(), m.market_category(), m.financial_status(), m.round_lot_size(),
        m.round_lots_only(), m.issue_classification(), m.issue_subtype().size(), m.authenticity(),
        m.short_sale_threshold(), m.ipo_flag(), m.luld_tier(), m.etp_flag(),
        m.etp_leverage_factor(), m.inverse_indicator());
  }
  void on(const itch::TradingAction& m) {
    mix(m, m.stock()[0], m.trading_state(), m.reserved(), m.reason()[3]);
  }
  void on(const itch::RegSho& m) { mix(m, m.stock()[7], m.action()); }
  void on(const itch::ParticipantPosition& m) {
    mix(m, m.mpid()[3], m.stock()[7], m.primary_market_maker(), m.market_maker_mode(),
        m.participant_state());
  }
  void on(const itch::MwcbDecline& m) { mix(m, m.level1(), m.level2(), m.level3()); }
  void on(const itch::MwcbStatus& m) { mix(m, m.breached_level()); }
  void on(const itch::IpoQuoting& m) {
    mix(m, m.stock()[7], m.release_time_s(), m.release_qualifier(), m.ipo_price());
  }
  void on(const itch::LuldCollar& m) {
    mix(m, m.stock()[7], m.reference_price(), m.upper_price(), m.lower_price(), m.extension());
  }
  void on(const itch::OperationalHalt& m) { mix(m, m.stock()[7], m.market_code(), m.action()); }
  void on(const itch::AddOrder& m) {
    mix(m, m.order_ref(), m.side(), m.shares(), m.stock()[7], m.price());
  }
  void on(const itch::AddOrderMpid& m) {
    mix(m, m.order_ref(), m.side(), m.shares(), m.stock()[7], m.price(), m.attribution()[3]);
  }
  void on(const itch::OrderExecuted& m) {
    mix(m, m.order_ref(), m.executed_shares(), m.match_number());
  }
  void on(const itch::OrderExecutedPrice& m) {
    mix(m, m.order_ref(), m.executed_shares(), m.match_number(), m.printable(),
        m.execution_price());
  }
  void on(const itch::OrderCancel& m) { mix(m, m.order_ref(), m.cancelled_shares()); }
  void on(const itch::OrderDelete& m) { mix(m, m.order_ref()); }
  void on(const itch::OrderReplace& m) {
    mix(m, m.original_ref(), m.new_ref(), m.shares(), m.price());
  }
  void on(const itch::Trade& m) {
    mix(m, m.order_ref(), m.side(), m.shares(), m.stock()[7], m.price(), m.match_number());
  }
  void on(const itch::CrossTrade& m) {
    mix(m, m.shares(), m.stock()[7], m.cross_price(), m.match_number(), m.cross_type());
  }
  void on(const itch::BrokenTrade& m) { mix(m, m.match_number()); }
  void on(const itch::Noii& m) {
    mix(m, m.paired_shares(), m.imbalance_shares(), m.imbalance_direction(), m.stock()[7],
        m.far_price(), m.near_price(), m.reference_price(), m.cross_type(), m.price_variation());
  }
  void on(const itch::Rpii& m) { mix(m, m.stock()[7], m.interest_flag()); }
  void on(const itch::Dlcr& m) {
    mix(m, m.stock()[7], m.open_eligibility(), m.minimum_allowable_price(),
        m.maximum_allowable_price(), m.near_execution_price(), m.near_execution_time(),
        m.lower_price_collar(), m.upper_price_collar());
  }

  [[nodiscard]] std::uint64_t value() const { return value_; }

 private:
  template <class View, class... Fields>
  void mix(const View& m, Fields... fields) {
    value_ += m.stock_locate() + m.tracking_number() + m.timestamp_ns();
    ((value_ += static_cast<std::uint64_t>(fields)), ...);
  }

  std::uint64_t value_ = 0;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  Touch touch;
  const auto result =
      itch::drive(bytes, touch, {.strict = false}, [](const itch::DriveResult&, std::uint64_t) {});
  volatile std::uint64_t sink = touch.value() + result.messages;
  static_cast<void>(sink);
  return 0;
}
