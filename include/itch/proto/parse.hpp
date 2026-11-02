#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "itch/proto/messages.hpp"

namespace itch {

enum class ParseStatus : std::uint8_t { Ok, UnknownType, BadLength };

/// A type with one on() per message view. The parser is a template over this, so parse-only and
/// book-build share one decoder with no virtual calls.
template <class H>
concept MessageHandler = requires(
    H& h, const SystemEvent& s, const StockDirectory& r, const TradingAction& a, const RegSho& y,
    const ParticipantPosition& l, const MwcbDecline& v, const MwcbStatus& w, const IpoQuoting& k,
    const LuldCollar& j, const OperationalHalt& o, const AddOrder& add, const AddOrderMpid& addm,
    const OrderExecuted& e, const OrderExecutedPrice& c, const OrderCancel& x, const OrderDelete& d,
    const OrderReplace& u, const Trade& p, const CrossTrade& q, const BrokenTrade& b, const Noii& i,
    const Rpii& n, const Dlcr& dl) {
  h.on(s);
  h.on(r);
  h.on(a);
  h.on(y);
  h.on(l);
  h.on(v);
  h.on(w);
  h.on(k);
  h.on(j);
  h.on(o);
  h.on(add);
  h.on(addm);
  h.on(e);
  h.on(c);
  h.on(x);
  h.on(d);
  h.on(u);
  h.on(p);
  h.on(q);
  h.on(b);
  h.on(i);
  h.on(n);
  h.on(dl);
};

/// Validates the frame length for its type byte (DQ-01) and calls the matching handler with a
/// non-owning view. A frame that fails either check reaches no handler.
template <MessageHandler H>
ParseStatus parse(std::span<const std::byte> msg, H& handler) noexcept {
  if (msg.empty()) {
    return ParseStatus::BadLength;
  }
  const auto type_byte = std::to_integer<std::size_t>(msg[0]);
  const std::size_t expected = kLengthByByte[type_byte];
  if (expected == 0) {
    return ParseStatus::UnknownType;
  }
  if (msg.size() != expected) {
    return ParseStatus::BadLength;
  }

  const Header header{msg.data()};
  switch (header.type()) {
    case MsgType::AddOrder:
      handler.on(AddOrder{header});
      break;
    case MsgType::AddOrderMpid:
      handler.on(AddOrderMpid{AddOrder{header}});
      break;
    case MsgType::OrderExecuted:
      handler.on(OrderExecuted{header});
      break;
    case MsgType::OrderExecutedPrice:
      handler.on(OrderExecutedPrice{header});
      break;
    case MsgType::OrderCancel:
      handler.on(OrderCancel{header});
      break;
    case MsgType::OrderDelete:
      handler.on(OrderDelete{header});
      break;
    case MsgType::OrderReplace:
      handler.on(OrderReplace{header});
      break;
    case MsgType::Trade:
      handler.on(Trade{header});
      break;
    case MsgType::CrossTrade:
      handler.on(CrossTrade{header});
      break;
    case MsgType::BrokenTrade:
      handler.on(BrokenTrade{header});
      break;
    case MsgType::Noii:
      handler.on(Noii{header});
      break;
    case MsgType::Rpii:
      handler.on(Rpii{header});
      break;
    case MsgType::Dlcr:
      handler.on(Dlcr{header});
      break;
    case MsgType::SystemEvent:
      handler.on(SystemEvent{header});
      break;
    case MsgType::StockDirectory:
      handler.on(StockDirectory{header});
      break;
    case MsgType::TradingAction:
      handler.on(TradingAction{header});
      break;
    case MsgType::RegSho:
      handler.on(RegSho{header});
      break;
    case MsgType::ParticipantPosition:
      handler.on(ParticipantPosition{header});
      break;
    case MsgType::MwcbDecline:
      handler.on(MwcbDecline{header});
      break;
    case MsgType::MwcbStatus:
      handler.on(MwcbStatus{header});
      break;
    case MsgType::IpoQuoting:
      handler.on(IpoQuoting{header});
      break;
    case MsgType::LuldCollar:
      handler.on(LuldCollar{header});
      break;
    case MsgType::OperationalHalt:
      handler.on(OperationalHalt{header});
      break;
  }
  return ParseStatus::Ok;
}

}  // namespace itch
