#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "itch/proto/endian.hpp"

namespace itch::mold {

inline constexpr std::size_t kHeaderBytes = 20;
inline constexpr std::size_t kSessionBytes = 10;
inline constexpr std::size_t kRequestBytes = 20;
inline constexpr std::uint16_t kHeartbeat = 0;
inline constexpr std::uint16_t kEndOfSession = 0xFFFF;

using Session = std::array<char, kSessionBytes>;

/// Downstream packet header: session (10), sequence of the first message (8), count (2), all
/// big-endian. MoldUDP64 1.00 "Downstream Packet".
struct PacketHeader {
  Session session{};
  std::uint64_t seq = 0;
  std::uint16_t count = 0;
};

/// Request packet, the same layout with the first sequence and count wanted. "Request Packet".
struct RetransRequest {
  Session session{};
  std::uint64_t seq = 0;
  std::uint16_t count = 0;
};

/// Pads or truncates text to the 10-character session field.
[[nodiscard]] inline Session make_session(std::string_view text) noexcept {
  Session session;
  session.fill(' ');
  std::memcpy(session.data(), text.data(), std::min(text.size(), session.size()));
  return session;
}

namespace detail {

inline void store_be16(std::byte* p, std::uint16_t v) noexcept {
  const std::uint16_t big = bswap(v);
  std::memcpy(p, &big, sizeof(big));
}

inline void store_be64(std::byte* p, std::uint64_t v) noexcept {
  const std::uint64_t big = bswap(v);
  std::memcpy(p, &big, sizeof(big));
}

}  // namespace detail

/// Writes the 20-byte layout shared by downstream headers and requests.
inline void encode(std::byte* out, const Session& session, std::uint64_t seq,
                   std::uint16_t count) noexcept {
  std::memcpy(out, session.data(), session.size());
  detail::store_be64(out + kSessionBytes, seq);
  detail::store_be16(out + kSessionBytes + 8, count);
}

/// False when the packet is too short to hold a header.
[[nodiscard]] inline bool decode_header(std::span<const std::byte> pkt,
                                        PacketHeader& out) noexcept {
  if (pkt.size() < kHeaderBytes) {
    return false;
  }
  std::memcpy(out.session.data(), pkt.data(), kSessionBytes);
  out.seq = load_be<std::uint64_t>(pkt.data() + kSessionBytes);
  out.count = load_be<std::uint16_t>(pkt.data() + kSessionBytes + 8);
  return true;
}

/// False unless the packet is exactly a request.
[[nodiscard]] inline bool decode_request(std::span<const std::byte> pkt,
                                         RetransRequest& out) noexcept {
  if (pkt.size() != kRequestBytes) {
    return false;
  }
  PacketHeader header;
  static_cast<void>(decode_header(pkt, header));
  out = {header.session, header.seq, header.count};
  return true;
}

/// Calls `f(message)` for each message block after the header. Stops and returns false at the
/// first block that runs past the packet, or when the blocks do not match the header's count;
/// messages before that point have already been delivered.
template <class F>
bool for_each_message(std::span<const std::byte> pkt, F&& f) noexcept {
  PacketHeader header;
  if (!decode_header(pkt, header) || header.count == kEndOfSession) {
    return false;
  }
  std::size_t offset = kHeaderBytes;
  for (std::uint16_t i = 0; i < header.count; ++i) {
    if (pkt.size() - offset < 2) {
      return false;
    }
    const std::size_t length = load_be<std::uint16_t>(pkt.data() + offset);
    offset += 2;
    if (pkt.size() - offset < length) {
      return false;
    }
    f(pkt.subspan(offset, length));
    offset += length;
  }
  return offset == pkt.size();
}

}  // namespace itch::mold
