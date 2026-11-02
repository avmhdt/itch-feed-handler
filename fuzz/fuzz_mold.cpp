// FR-27: arbitrary datagrams into the MoldUDP64 receiver. The input is cut into packets by a
// two-byte size, and delivered messages go on to the parser as they would in `itch listen`.

#include <cstddef>
#include <cstdint>
#include <span>

#include "itch/net/mold_receiver.hpp"
#include "itch/proto/parse.hpp"

namespace {

struct Sink {
  template <class View>
  void on(const View& message) {
    total += message.timestamp_ns();
  }
  std::uint64_t total = 0;
};

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const auto bytes = std::as_bytes(std::span(data, size));
  itch::MoldReceiver receiver(itch::ReceiverOptions{
      .max_buffered_packets = 64, .rerequest_timeout_ns = 10, .rerequest_max_retries = 3});
  Sink sink;
  std::uint64_t asked = 0;
  std::int64_t now = 0;
  const auto deliver = [&sink](std::span<const std::byte> message) {
    static_cast<void>(itch::parse(message, sink));
  };
  const auto ask = [&asked](std::uint64_t seq, std::uint16_t count) { asked += seq + count; };

  std::size_t offset = 0;
  while (size - offset >= 2) {
    const std::size_t length = (std::to_integer<std::size_t>(bytes[offset]) << 8U) |
                               std::to_integer<std::size_t>(bytes[offset + 1]);
    offset += 2;
    const std::size_t take = length < size - offset ? length : size - offset;
    now += 3;
    if (receiver.on_packet(bytes.subspan(offset, take), now, deliver, ask) !=
        itch::MoldReceiver::Status::Running) {
      break;
    }
    static_cast<void>(receiver.on_tick(now, ask));
    offset += take;
  }
  volatile std::uint64_t keep = sink.total + asked + receiver.stats().messages;
  static_cast<void>(keep);
  return 0;
}
