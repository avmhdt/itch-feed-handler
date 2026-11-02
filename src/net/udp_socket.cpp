#include "itch/net/udp_socket.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace itch {

namespace {

constexpr std::uint32_t kLoopbackNet = 0x7F000000U;
constexpr std::uint32_t kLoopbackMask = 0xFF000000U;

[[noreturn]] void throw_errno(const char* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

in_addr loopback_address(const std::string& text) {
  in_addr address{};
  if (::inet_pton(AF_INET, text.c_str(), &address) != 1) {
    throw std::invalid_argument("bad interface address: " + text);
  }
  if ((ntohl(address.s_addr) & kLoopbackMask) != kLoopbackNet) {
    throw std::invalid_argument("refusing non-loopback interface: " + text);
  }
  return address;
}

void set_option(int fd, int level, int name, const void* value, socklen_t size, const char* what) {
  if (::setsockopt(fd, level, name, value, size) != 0) {
    throw_errno(what);
  }
}

}  // namespace

bool Endpoint::multicast() const noexcept { return IN_MULTICAST(ntohl(address.sin_addr.s_addr)); }

std::string Endpoint::text() const {
  std::array<char, INET_ADDRSTRLEN> ip{};
  ::inet_ntop(AF_INET, &address.sin_addr, ip.data(), ip.size());
  return std::string(ip.data()) + ":" + std::to_string(ntohs(address.sin_port));
}

Endpoint parse_endpoint(const std::string& text) {
  const std::size_t colon = text.rfind(':');
  Endpoint endpoint;
  endpoint.address.sin_family = AF_INET;
  std::uint16_t port = 0;
  if (colon == std::string::npos) {
    throw std::invalid_argument("expected ADDRESS:PORT, got " + text);
  }
  const std::string_view digits = std::string_view(text).substr(colon + 1);
  const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), port);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() ||
      ::inet_pton(AF_INET, text.substr(0, colon).c_str(), &endpoint.address.sin_addr) != 1) {
    throw std::invalid_argument("expected ADDRESS:PORT, got " + text);
  }
  endpoint.address.sin_port = htons(port);
  return endpoint;
}

UdpSocket::UdpSocket() : fd_(::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0)) {
  if (fd_ < 0) {
    throw_errno("socket");
  }
}

UdpSocket::~UdpSocket() { ::close(fd_); }

void UdpSocket::configure_sender(const std::string& iface) {
  const in_addr address = loopback_address(iface);
  const unsigned char ttl = 0;
  const unsigned char loop = 1;
  set_option(fd_, IPPROTO_IP, IP_MULTICAST_IF, &address, sizeof(address), "IP_MULTICAST_IF");
  set_option(fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl), "IP_MULTICAST_TTL");
  set_option(fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop), "IP_MULTICAST_LOOP");
}

void UdpSocket::bind_receiver(const Endpoint& group, const std::string& iface) {
  const int reuse = 1;
  set_option(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse), "SO_REUSEADDR");
  sockaddr_in local = group.address;
  local.sin_addr.s_addr = htonl(group.multicast() ? INADDR_ANY : INADDR_LOOPBACK);
  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&local),  // NOLINT(*-reinterpret-cast)
             sizeof(local)) != 0) {
    throw_errno("bind");
  }
  if (group.multicast()) {
    ip_mreq membership{};
    membership.imr_multiaddr = group.address.sin_addr;
    membership.imr_interface = loopback_address(iface);
    set_option(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership),
               "IP_ADD_MEMBERSHIP");
  }
}

void UdpSocket::bind_loopback(std::uint16_t port) {
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_port = htons(port);
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&local),  // NOLINT(*-reinterpret-cast)
             sizeof(local)) != 0) {
    throw_errno("bind");
  }
}

void UdpSocket::set_receive_buffer(int bytes) {
  set_option(fd_, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes), "SO_RCVBUF");
}

bool UdpSocket::send(const Endpoint& to, std::span<const std::byte> first,
                     std::span<const std::byte> second) noexcept {
  std::array<iovec, 2> parts{};
  parts[0] = {const_cast<std::byte*>(first.data()), first.size()};
  parts[1] = {const_cast<std::byte*>(second.data()), second.size()};
  msghdr message{};
  sockaddr_in target = to.address;
  message.msg_name = &target;
  message.msg_namelen = sizeof(target);
  message.msg_iov = parts.data();
  message.msg_iovlen = second.empty() ? 1 : 2;
  return ::sendmsg(fd_, &message, 0) >= 0;
}

long UdpSocket::receive(std::span<std::byte> buffer, Endpoint* from, int timeout_ms) noexcept {
  sockaddr_in source{};
  socklen_t source_size = sizeof(source);
  const long got = ::recvfrom(fd_, buffer.data(), buffer.size(), MSG_DONTWAIT,
                              reinterpret_cast<sockaddr*>(&source),  // NOLINT(*-reinterpret-cast)
                              &source_size);
  if (got >= 0 || timeout_ms == 0) {
    if (got >= 0 && from != nullptr) {
      from->address = source;
    }
    return got;
  }
  pollfd waiter{fd_, POLLIN, 0};
  if (::poll(&waiter, 1, timeout_ms) <= 0) {
    return -1;
  }
  return receive(buffer, from, 0);
}

}  // namespace itch
