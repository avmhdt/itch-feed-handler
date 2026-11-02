#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include "itch/net/moldudp64.hpp"
#include "itch/net/udp_socket.hpp"

namespace itch {

/// Where each recent message sits in the mapped file, by sequence number. The file bytes are
/// already a run of length-prefixed blocks, so nothing is copied until a request arrives.
class SequenceWindow {
 public:
  explicit SequenceWindow(std::size_t capacity) : offsets_(capacity) {}

  /// Publisher side: records message `seq`, which must follow the previous one.
  void record(std::uint64_t seq, std::uint64_t offset) noexcept {
    offsets_[seq % offsets_.size()].store(offset, std::memory_order_relaxed);
    newest_.store(seq, std::memory_order_release);
  }

  [[nodiscard]] std::uint64_t newest() const noexcept {
    return newest_.load(std::memory_order_acquire);
  }
  /// Oldest sequence still held, or 1 before the window has wrapped.
  [[nodiscard]] std::uint64_t oldest() const noexcept {
    const std::uint64_t latest = newest();
    return latest >= offsets_.size() ? latest - offsets_.size() + 1 : 1;
  }
  [[nodiscard]] std::uint64_t offset(std::uint64_t seq) const noexcept {
    return offsets_[seq % offsets_.size()].load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::size_t capacity() const noexcept { return offsets_.size(); }

 private:
  std::vector<std::atomic<std::uint64_t>> offsets_;
  std::atomic<std::uint64_t> newest_{0};
};

/// Answers MoldUDP64 request packets over unicast from a SequenceWindow (FR-22). A request answers
/// with one packet of as many messages as fit, and the receiver asks again for the rest, as the
/// protocol describes.
class RetransmitServer {
 public:
  RetransmitServer(std::span<const std::byte> file, const SequenceWindow& window,
                   const mold::Session& session, std::uint16_t port, std::size_t max_payload);
  ~RetransmitServer();

  RetransmitServer(const RetransmitServer&) = delete;
  RetransmitServer& operator=(const RetransmitServer&) = delete;

  void stop();
  [[nodiscard]] std::uint64_t requests() const noexcept {
    return requests_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t refused() const noexcept {
    return refused_.load(std::memory_order_relaxed);
  }

 private:
  void run();
  void answer(const mold::RetransRequest& request, const Endpoint& to);

  std::span<const std::byte> file_;
  const SequenceWindow& window_;
  mold::Session session_;
  std::size_t max_payload_;
  UdpSocket socket_;
  std::atomic<bool> stopping_{false};
  std::atomic<std::uint64_t> requests_{0};
  std::atomic<std::uint64_t> refused_{0};
  std::thread thread_;
};

}  // namespace itch
