#pragma once

#include <netinet/in.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace itch {

struct Endpoint {
  sockaddr_in address{};

  [[nodiscard]] bool multicast() const noexcept;
  [[nodiscard]] std::string text() const;
};

/// Parses "A.B.C.D:PORT"; throws std::invalid_argument otherwise.
[[nodiscard]] Endpoint parse_endpoint(const std::string& text);

/// A UDP socket held to the safety rule of this repo: traffic stays on loopback (spec section 16).
/// A multicast sender uses TTL 0 and the loopback interface, and non-loopback addresses are
/// refused. A non-multicast address is the unicast fallback of R-01, with identical framing.
class UdpSocket {
 public:
  UdpSocket();
  ~UdpSocket();

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  /// Sends to `group` over `iface`, which must be a loopback address.
  void configure_sender(const std::string& iface);
  /// Binds `port` on all addresses and joins `group` over `iface` when it is multicast. One socket
  /// gets both the feed and the unicast retransmissions.
  void bind_receiver(const Endpoint& group, const std::string& iface);
  /// Binds a unicast port on the loopback address, for the retransmission server.
  void bind_loopback(std::uint16_t port);
  /// Asks for up to `bytes` of kernel receive buffer; the kernel may give less.
  void set_receive_buffer(int bytes);

  /// Scatter send of a header and a body as one datagram; returns false if the kernel refused.
  bool send(const Endpoint& to, std::span<const std::byte> first,
            std::span<const std::byte> second) noexcept;
  /// Waits up to `timeout_ms` for a datagram; returns its size, or -1 when nothing arrived.
  long receive(std::span<std::byte> buffer, Endpoint* from, int timeout_ms) noexcept;

 private:
  int fd_;
};

}  // namespace itch
