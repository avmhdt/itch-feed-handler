#include "itch/state/instruments.hpp"

#include <algorithm>

namespace itch {

void LocateTable::define(const StockDirectory& message) noexcept {
  Instrument& instrument = instruments_[message.stock_locate()];
  const std::string_view stock = message.stock();
  const std::string_view subtype = message.issue_subtype();
  std::copy(stock.begin(), stock.end(), instrument.stock.begin());
  std::copy(subtype.begin(), subtype.end(), instrument.issue_subtype.begin());
  instrument.round_lot_size = message.round_lot_size();
  instrument.locate = message.stock_locate();
  instrument.market_category = message.market_category();
  instrument.financial_status = message.financial_status();
  instrument.issue_classification = message.issue_classification();
  instrument.authenticity = message.authenticity();
  instrument.luld_tier = message.luld_tier();
  instrument.etp_flag = message.etp_flag();
  instrument.defined = true;
}

std::uint16_t LocateTable::find(std::string_view symbol) const noexcept {
  for (const Instrument& instrument : instruments_) {
    if (instrument.defined && instrument.locate != 0 && instrument.symbol() == symbol) {
      return instrument.locate;
    }
  }
  return 0;
}

std::vector<const Instrument*> LocateTable::all() const {
  std::vector<const Instrument*> out;
  for (const Instrument& instrument : instruments_) {
    if (instrument.defined) {
      out.push_back(&instrument);
    }
  }
  return out;
}

}  // namespace itch
