#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "itch/net/moldudp64.hpp"

namespace itch {

struct ReceiverOptions {
  std::size_t max_buffered_packets = std::size_t{1} << 18;
  std::int64_t rerequest_timeout_ns = 50'000'000;
  int rerequest_max_retries = 20;
};

/// One second of the gap timeline.
struct GapBucket {
  std::uint64_t messages = 0;
  std::uint64_t gaps = 0;
  std::uint64_t rerequests = 0;
};

struct ReceiverStats {
  std::uint64_t packets = 0;
  std::uint64_t messages = 0;
  std::uint64_t malformed_packets = 0;
  std::uint64_t wrong_session = 0;
  std::uint64_t duplicate_packets = 0;
  std::uint64_t buffered_packets = 0;
  std::uint64_t overflow_packets = 0;
  std::uint64_t heartbeats = 0;
  std::uint64_t gaps = 0;
  std::uint64_t gap_messages = 0;
  std::uint64_t rerequests = 0;
  std::uint64_t timeouts = 0;
  std::uint64_t largest_gap = 0;
  std::vector<GapBucket> timeline;
};

/// MoldUDP64 sequencing (FR-23), kept apart from sockets so that every path can be tested and
/// fuzzed: tracks the expected sequence, buffers packets past a gap up to a bound, asks again for
/// what is missing, drops duplicates, and delivers messages strictly in order. It follows the
/// "Receiver Example" in the MoldUDP64 specification.
class MoldReceiver {
 public:
  enum class Status { Running, Finished, Unrecoverable };

  explicit MoldReceiver(const ReceiverOptions& options = {}) : options_(options) {}

  /// Handles one datagram. `deliver(message)` gets each new message in order;
  /// `request(seq, count)` should send a re-request.
  template <class Deliver, class Request>
  Status on_packet(std::span<const std::byte> packet, std::int64_t now_ns, Deliver&& deliver,
                   Request&& request);
  /// Runs the re-request timer; call it when no packet is waiting.
  template <class Request>
  Status on_tick(std::int64_t now_ns, Request&& request);

  [[nodiscard]] std::uint64_t expected() const noexcept { return expected_; }
  [[nodiscard]] const ReceiverStats& stats() const noexcept { return stats_; }
  [[nodiscard]] std::uint64_t unrecovered_from() const noexcept { return expected_; }

 private:
  using Packet = std::vector<std::byte>;

  template <class Deliver>
  void deliver_from(std::span<const std::byte> packet, const mold::PacketHeader& header,
                    Deliver& deliver, std::int64_t now_ns);
  template <class Deliver>
  void drain_buffer(Deliver& deliver, std::int64_t now_ns);
  template <class Request>
  void ask(std::int64_t now_ns, Request& request);
  void note_gap_end(std::uint64_t end) noexcept;
  GapBucket& bucket(std::int64_t now_ns);
  [[nodiscard]] Status status() const noexcept;

  static constexpr std::int64_t kBucketNs = 1'000'000'000;

  ReceiverOptions options_;
  ReceiverStats stats_;
  std::map<std::uint64_t, Packet> buffered_;
  std::optional<mold::Session> session_;
  std::uint64_t expected_ = 1;
  std::uint64_t known_end_ = 1;  ///< One past the highest sequence known to exist.
  std::uint64_t final_seq_ = 0;  ///< Next sequence announced by end of session; 0 until then.
  std::uint64_t asked_seq_ = 0;
  std::int64_t asked_at_ns_ = 0;
  std::int64_t origin_ns_ = -1;
  int retries_ = 0;
  bool failed_ = false;
};

inline void MoldReceiver::note_gap_end(std::uint64_t end) noexcept {
  if (end > known_end_) {
    if (known_end_ == expected_) {
      ++stats_.gaps;
    }
    known_end_ = end;
  }
}

inline GapBucket& MoldReceiver::bucket(std::int64_t now_ns) {
  if (origin_ns_ < 0) {
    origin_ns_ = now_ns;
  }
  const auto index = static_cast<std::size_t>((now_ns - origin_ns_) / kBucketNs);
  if (index >= stats_.timeline.size()) {
    stats_.timeline.resize(index + 1);
  }
  return stats_.timeline[index];
}

inline MoldReceiver::Status MoldReceiver::status() const noexcept {
  if (failed_) {
    return Status::Unrecoverable;
  }
  return final_seq_ != 0 && expected_ >= final_seq_ ? Status::Finished : Status::Running;
}

template <class Deliver>
void MoldReceiver::deliver_from(std::span<const std::byte> packet, const mold::PacketHeader& header,
                                Deliver& deliver, std::int64_t now_ns) {
  std::uint64_t seq = header.seq;
  GapBucket& current = bucket(now_ns);
  mold::for_each_message(packet, [&](std::span<const std::byte> message) {
    if (seq == expected_) {
      deliver(message);
      ++expected_;
      ++stats_.messages;
      ++current.messages;
    }
    ++seq;
  });
}

template <class Deliver>
void MoldReceiver::drain_buffer(Deliver& deliver, std::int64_t now_ns) {
  while (!buffered_.empty() && buffered_.begin()->first <= expected_) {
    const Packet packet = std::move(buffered_.begin()->second);
    buffered_.erase(buffered_.begin());
    mold::PacketHeader header;
    if (mold::decode_header(packet, header)) {
      deliver_from(packet, header, deliver, now_ns);
    }
  }
}

template <class Request>
void MoldReceiver::ask(std::int64_t now_ns, Request& request) {
  if (expected_ >= known_end_) {
    return;
  }
  // Ask only up to the first buffered packet: what lies beyond it has already arrived.
  std::uint64_t end = known_end_;
  if (!buffered_.empty()) {
    end = std::min(end, buffered_.begin()->first);
  }
  const std::uint64_t missing = end > expected_ ? end - expected_ : 1;
  if (asked_seq_ == expected_) {
    if (now_ns - asked_at_ns_ < options_.rerequest_timeout_ns) {
      return;
    }
    ++stats_.timeouts;
    if (++retries_ > options_.rerequest_max_retries) {
      failed_ = true;
      return;
    }
  } else {
    retries_ = 0;
  }
  asked_seq_ = expected_;
  asked_at_ns_ = now_ns;
  ++stats_.rerequests;
  ++bucket(now_ns).rerequests;
  stats_.largest_gap = std::max(stats_.largest_gap, missing);
  request(expected_, static_cast<std::uint16_t>(std::min<std::uint64_t>(missing, 0xFFFE)));
}

template <class Deliver, class Request>
MoldReceiver::Status MoldReceiver::on_packet(std::span<const std::byte> packet, std::int64_t now_ns,
                                             Deliver&& deliver, Request&& request) {
  mold::PacketHeader header;
  if (!mold::decode_header(packet, header)) {
    ++stats_.malformed_packets;
    return status();
  }
  if (!session_) {
    session_ = header.session;
  } else if (*session_ != header.session) {
    ++stats_.wrong_session;
    return status();
  }
  ++stats_.packets;

  if (header.count == mold::kHeartbeat || header.count == mold::kEndOfSession) {
    // Both carry the next sequence, so they reveal loss at the tail of the stream.
    stats_.heartbeats += header.count == mold::kHeartbeat ? 1 : 0;
    if (header.count == mold::kEndOfSession) {
      final_seq_ = std::max(final_seq_, std::max<std::uint64_t>(header.seq, 1));
    }
    const std::uint64_t before = stats_.gaps;
    note_gap_end(header.seq);
    bucket(now_ns).gaps += stats_.gaps - before;
    ask(now_ns, request);
    return status();
  }

  const std::uint64_t end = header.seq + header.count;
  if (end < header.seq || header.seq == 0) {
    ++stats_.malformed_packets;
    return status();
  }
  if (end <= expected_) {
    ++stats_.duplicate_packets;
    return status();
  }
  if (header.seq <= expected_) {
    const std::uint64_t before = expected_;
    deliver_from(packet, header, deliver, now_ns);
    if (expected_ == before) {
      ++stats_.malformed_packets;
      return status();
    }
    known_end_ = std::max(known_end_, expected_);
    drain_buffer(deliver, now_ns);
  } else {
    const std::uint64_t before = stats_.gaps;
    note_gap_end(header.seq);
    if (stats_.gaps != before) {
      stats_.gap_messages += header.seq - expected_;
      ++bucket(now_ns).gaps;
    }
    if (buffered_.size() < options_.max_buffered_packets) {
      buffered_.try_emplace(header.seq, packet.begin(), packet.end());
      ++stats_.buffered_packets;
    } else {
      ++stats_.overflow_packets;
    }
    note_gap_end(end);
  }
  ask(now_ns, request);
  return status();
}

template <class Request>
MoldReceiver::Status MoldReceiver::on_tick(std::int64_t now_ns, Request&& request) {
  ask(now_ns, request);
  return status();
}

}  // namespace itch
