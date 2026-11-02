#include "itch/net/mold_publisher.hpp"

#include <array>
#include <chrono>
#include <thread>

#include "itch/io/frame_reader.hpp"
#include "itch/net/retransmit_server.hpp"
#include "itch/proto/endian.hpp"

namespace itch {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kEndOfSessionGapMs = 100;
constexpr std::size_t kTimestampOffset = 5;
constexpr std::size_t kMinimumHeader = 11;

/// Holds back the publisher, by message count or by ITCH timestamps, sending heartbeats meanwhile.
class Pacer {
 public:
  Pacer(const PublisherOptions& options, UdpSocket& socket, std::uint64_t& heartbeats)
      : options_(options), socket_(socket), heartbeats_(heartbeats), start_(Clock::now()) {}

  void before_packet(std::uint64_t sent_messages, std::uint64_t next_seq,
                     std::uint64_t first_timestamp) {
    Clock::time_point due = start_;
    if (options_.speed > 0.0) {
      if (!have_origin_) {
        have_origin_ = true;
        origin_timestamp_ = first_timestamp;
      }
      const auto elapsed_ns = static_cast<double>(first_timestamp - origin_timestamp_);
      due += std::chrono::nanoseconds(static_cast<std::int64_t>(elapsed_ns / options_.speed));
    } else if (options_.max_messages_per_s > 0) {
      due += std::chrono::nanoseconds(
          static_cast<std::int64_t>(static_cast<double>(sent_messages) * 1e9 /
                                    static_cast<double>(options_.max_messages_per_s)));
    } else {
      return;
    }
    wait_until(due, next_seq);
  }

 private:
  void wait_until(Clock::time_point due, std::uint64_t next_seq) {
    using std::chrono::milliseconds;
    while (Clock::now() < due) {
      const auto now = Clock::now();
      if (now - last_heartbeat_ >= milliseconds(options_.heartbeat_ms)) {
        send_heartbeat(next_seq);
        last_heartbeat_ = now;
      }
      if (due - now > milliseconds(2)) {
        std::this_thread::sleep_for(milliseconds(1));
      } else {
        std::this_thread::yield();
      }
    }
  }

  void send_heartbeat(std::uint64_t next_seq) {
    std::array<std::byte, mold::kHeaderBytes> header{};
    mold::encode(header.data(), options_.session, next_seq, mold::kHeartbeat);
    socket_.send(options_.group, header, {});
    ++heartbeats_;
  }

  const PublisherOptions& options_;
  UdpSocket& socket_;
  std::uint64_t& heartbeats_;
  Clock::time_point start_;
  Clock::time_point last_heartbeat_ = Clock::now();
  std::uint64_t origin_timestamp_ = 0;
  bool have_origin_ = false;
};

}  // namespace

PublisherStats publish(std::span<const std::byte> file, const PublisherOptions& options,
                       DropPlan& plan) {
  UdpSocket socket;
  if (options.group.multicast()) {
    socket.configure_sender(options.iface);
  }
  SequenceWindow window(options.retrans_buffer_msgs);
  RetransmitServer server(file, window, options.session, options.retrans_port, options.max_payload);

  PublisherStats stats;
  Pacer pacer(options, socket, stats.heartbeats);
  FrameReader reader(file);
  std::span<const std::byte> frame;
  std::uint64_t next_seq = 1;
  std::array<std::byte, mold::kHeaderBytes> header{};
  bool more = true;

  while (more) {
    const std::uint64_t first_seq = next_seq;
    const std::uint64_t first_offset = reader.offset();
    std::uint64_t first_timestamp = 0;
    std::size_t bytes = 0;
    std::uint16_t count = 0;

    // Fill one packet. A frame that does not fit is left for the next one by peeking.
    while (true) {
      if (options.limit != 0 && next_seq > options.limit) {
        more = false;
        break;
      }
      FrameReader peek = reader;
      if (peek.next(frame) != FrameReader::Next::Frame) {
        more = false;
        break;
      }
      const std::size_t block = frame.size() + 2;
      if (count != 0 && (bytes + block > options.max_payload || count == mold::kEndOfSession - 1)) {
        break;
      }
      if (count == 0 && frame.size() >= kMinimumHeader) {
        first_timestamp = load_be48(frame.data() + kTimestampOffset);
      }
      window.record(next_seq, reader.offset());
      reader = peek;
      bytes += block;
      ++count;
      ++next_seq;
    }
    if (count == 0) {
      break;
    }

    pacer.before_packet(stats.messages, first_seq, first_timestamp);
    if (!plan.should_drop(first_seq, count)) {
      mold::encode(header.data(), options.session, first_seq, count);
      socket.send(options.group, header, file.subspan(first_offset, bytes));
    }
    stats.messages += count;
    ++stats.packets;
  }

  // End of session persists for a while, so that a receiver can still fill its last gaps.
  const auto end = Clock::now() + std::chrono::milliseconds(options.linger_ms);
  mold::encode(header.data(), options.session, next_seq, mold::kEndOfSession);
  do {
    socket.send(options.group, header, {});
    std::this_thread::sleep_for(std::chrono::milliseconds(kEndOfSessionGapMs));
  } while (Clock::now() < end);

  server.stop();
  stats.dropped_packets = plan.dropped();
  stats.retrans_requests = server.requests();
  stats.retrans_refused = server.refused();
  return stats;
}

}  // namespace itch
