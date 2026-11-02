#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "itch/net/drop_plan.hpp"
#include "itch/net/moldudp64.hpp"
#include "itch/net/udp_socket.hpp"

namespace itch {

struct PublisherOptions {
  Endpoint group;
  std::string iface = "127.0.0.1";
  std::uint16_t retrans_port = 30002;
  mold::Session session = mold::make_session("ITCHDEMO01");
  std::size_t max_payload = 1400;
  double speed = 0.0;                    ///< Multiple of real time; 0 means as fast as allowed.
  std::uint64_t max_messages_per_s = 0;  ///< Cap for `max` rate; 0 means none.
  int heartbeat_ms = 1000;
  std::size_t retrans_buffer_msgs = 2'000'000;
  int linger_ms = 3000;  ///< How long end-of-session persists for last re-requests.
  std::uint64_t limit = 0;
};

struct PublisherStats {
  std::uint64_t messages = 0;
  std::uint64_t packets = 0;
  std::uint64_t dropped_packets = 0;
  std::uint64_t heartbeats = 0;
  std::uint64_t retrans_requests = 0;
  std::uint64_t retrans_refused = 0;
};

/// Replays a mapped ITCH file as MoldUDP64 (FR-22). Consecutive file frames are already valid
/// message blocks, so each packet is a 20-byte header plus a slice of the file.
[[nodiscard]] PublisherStats publish(std::span<const std::byte> file,
                                     const PublisherOptions& options, DropPlan& plan);

}  // namespace itch
