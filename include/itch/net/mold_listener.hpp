#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <thread>

#include "itch/net/mold_receiver.hpp"
#include "itch/net/udp_socket.hpp"
#include "itch/proto/parse.hpp"
#include "itch/util/spsc_queue.hpp"

namespace itch {

struct ListenOptions {
  Endpoint group;
  Endpoint retrans;
  std::string iface = "127.0.0.1";
  ReceiverOptions receiver;
  std::size_t ring_packets = std::size_t{1} << 16;
  int idle_timeout_ms = 30'000;  ///< Give up after this long without a single packet.
};

struct ListenResult {
  MoldReceiver::Status status = MoldReceiver::Status::Running;
  ReceiverStats stats;
  std::uint64_t expected = 1;
  std::uint64_t malformed_messages = 0;
  bool idle_timeout = false;
};

/// A UDP payload as received, handed from the receive thread to the hot thread.
struct PacketSlot {
  static constexpr std::size_t kCapacity = 2048;
  std::array<std::byte, kCapacity> bytes;
  std::size_t size = 0;
};

/// Receives until end of session or an unrecoverable gap, passing each message in order through
/// the same parser as file mode. One thread reads the socket into a ring so the kernel buffer is
/// kept empty, and the calling thread runs the book.
template <MessageHandler H>
ListenResult listen(const ListenOptions& options, H& handler) {
  using Clock = std::chrono::steady_clock;

  UdpSocket socket;
  socket.bind_receiver(options.group, options.iface);
  socket.set_receive_buffer(64 * 1024 * 1024);

  SpscQueue<PacketSlot> ring(options.ring_packets);
  std::atomic<bool> stopping{false};
  std::thread reader([&] {
    PacketSlot slot{};
    constexpr int kPollMs = 5;
    while (!stopping.load(std::memory_order_acquire)) {
      const long got = socket.receive(slot.bytes, nullptr, kPollMs);
      if (got <= 0) {
        continue;
      }
      slot.size = static_cast<std::size_t>(got);
      while (!ring.try_push(slot)) {
        if (stopping.load(std::memory_order_acquire)) {
          return;
        }
        std::this_thread::yield();
      }
    }
  });

  ListenResult result;
  MoldReceiver receiver(options.receiver);
  std::array<std::byte, mold::kRequestBytes> request{};
  mold::Session session = mold::make_session("");
  PacketSlot packet{};
  bool session_known = false;

  const auto ask = [&](std::uint64_t seq, std::uint16_t count) {
    mold::encode(request.data(), session, seq, count);
    socket.send(options.retrans, request, {});
  };
  const auto deliver = [&](std::span<const std::byte> message) {
    if (parse(message, handler) != ParseStatus::Ok) {
      ++result.malformed_messages;
    }
  };
  const auto now_ns = [] {
    const auto now = Clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  };

  auto last_packet = Clock::now();
  MoldReceiver::Status status = MoldReceiver::Status::Running;
  while (status == MoldReceiver::Status::Running) {
    if (ring.try_pop(packet)) {
      last_packet = Clock::now();
      const std::span<const std::byte> view(packet.bytes.data(), packet.size);
      if (!session_known && packet.size >= mold::kSessionBytes) {
        std::copy_n(reinterpret_cast<const char*>(packet.bytes.data()),  // NOLINT
                    mold::kSessionBytes, session.begin());
        session_known = true;
      }
      status = receiver.on_packet(view, now_ns(), deliver, ask);
    } else {
      status = receiver.on_tick(now_ns(), ask);
      const auto idle = Clock::now() - last_packet;
      if (idle > std::chrono::milliseconds(options.idle_timeout_ms)) {
        result.idle_timeout = true;
        break;
      }
      std::this_thread::yield();
    }
  }

  stopping.store(true, std::memory_order_release);
  reader.join();
  result.status = status;
  result.stats = receiver.stats();
  result.expected = receiver.expected();
  return result;
}

}  // namespace itch
