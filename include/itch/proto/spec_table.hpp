#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace itch {

/// ITCH 5.0 message type bytes, in the order of the specification's section 1.
enum class MsgType : char {
  SystemEvent = 'S',          // 1.1
  StockDirectory = 'R',       // 1.2.1
  TradingAction = 'H',        // 1.2.2
  RegSho = 'Y',               // 1.2.3
  ParticipantPosition = 'L',  // 1.2.4
  MwcbDecline = 'V',          // 1.2.5.1
  MwcbStatus = 'W',           // 1.2.5.2
  IpoQuoting = 'K',           // 1.2.6
  LuldCollar = 'J',           // 1.2.7
  OperationalHalt = 'h',      // 1.2.8
  AddOrder = 'A',             // 1.3.1
  AddOrderMpid = 'F',         // 1.3.2
  OrderExecuted = 'E',        // 1.4.1
  OrderExecutedPrice = 'C',   // 1.4.2
  OrderCancel = 'X',          // 1.4.3
  OrderDelete = 'D',          // 1.4.4
  OrderReplace = 'U',         // 1.4.5
  Trade = 'P',                // 1.5.1
  CrossTrade = 'Q',           // 1.5.2
  BrokenTrade = 'B',          // 1.5.3
  Noii = 'I',                 // 1.6
  Rpii = 'N',                 // 1.7
  Dlcr = 'O',                 // 1.8
};

struct TypeLength {
  MsgType type;
  std::uint8_t length;  // Bytes including the type byte.
};

/// Every message type with its length, each checked against the field tables of the ITCH 5.0 PDF
/// (revision of April 28, 2023). messages.hpp asserts each view against this table.
inline constexpr std::array<TypeLength, 23> kTypeLengths{{
    {MsgType::SystemEvent, 12},
    {MsgType::StockDirectory, 39},
    {MsgType::TradingAction, 25},
    {MsgType::RegSho, 20},
    {MsgType::ParticipantPosition, 26},
    {MsgType::MwcbDecline, 35},
    {MsgType::MwcbStatus, 12},
    {MsgType::IpoQuoting, 28},
    {MsgType::LuldCollar, 35},
    {MsgType::OperationalHalt, 21},
    {MsgType::AddOrder, 36},
    {MsgType::AddOrderMpid, 40},
    {MsgType::OrderExecuted, 31},
    {MsgType::OrderExecutedPrice, 36},
    {MsgType::OrderCancel, 23},
    {MsgType::OrderDelete, 19},
    {MsgType::OrderReplace, 35},
    {MsgType::Trade, 44},
    {MsgType::CrossTrade, 40},
    {MsgType::BrokenTrade, 19},
    {MsgType::Noii, 50},
    {MsgType::Rpii, 20},
    {MsgType::Dlcr, 48},
}};

/// Length by type byte; 0 marks an unknown type. A flat table keeps the check to one load.
inline constexpr std::array<std::uint8_t, 256> kLengthByByte = [] {
  std::array<std::uint8_t, 256> table{};
  for (const TypeLength& entry : kTypeLengths) {
    table[static_cast<unsigned char>(entry.type)] = entry.length;
  }
  return table;
}();

/// Message length including the type byte; 0 for an unknown type.
[[nodiscard]] constexpr std::size_t message_length(MsgType t) noexcept {
  return kLengthByByte[static_cast<unsigned char>(t)];
}

}  // namespace itch
