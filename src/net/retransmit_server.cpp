#include "itch/net/retransmit_server.hpp"

#include <array>

namespace itch {

RetransmitServer::RetransmitServer(std::span<const std::byte> file, const SequenceWindow& window,
                                   const mold::Session& session, std::uint16_t port,
                                   std::size_t max_payload)
    : file_(file), window_(window), session_(session), max_payload_(max_payload) {
  socket_.bind_loopback(port);
  socket_.set_receive_buffer(8 * 1024 * 1024);
  thread_ = std::thread([this] { run(); });
}

RetransmitServer::~RetransmitServer() { stop(); }

void RetransmitServer::stop() {
  stopping_.store(true, std::memory_order_release);
  if (thread_.joinable()) {
    thread_.join();
  }
}

void RetransmitServer::run() {
  std::array<std::byte, 64> buffer{};
  constexpr int kPollMs = 20;
  while (!stopping_.load(std::memory_order_acquire)) {
    Endpoint from;
    const long got = socket_.receive(buffer, &from, kPollMs);
    mold::RetransRequest request;
    if (got > 0 &&
        mold::decode_request(std::span(buffer).first(static_cast<std::size_t>(got)), request)) {
      answer(request, from);
    }
  }
}

void RetransmitServer::answer(const mold::RetransRequest& request, const Endpoint& to) {
  requests_.fetch_add(1, std::memory_order_relaxed);
  const std::uint64_t newest = window_.newest();
  if (request.session != session_ || request.count == 0 || request.seq > newest ||
      request.seq < window_.oldest()) {
    refused_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  // The file already holds length-prefixed blocks in sequence, so a run of messages is one slice.
  const std::uint64_t first = window_.offset(request.seq);
  std::size_t bytes = 0;
  std::uint16_t count = 0;
  for (std::uint64_t seq = request.seq; seq <= newest && count < request.count; ++seq) {
    const std::uint64_t offset = window_.offset(seq);
    const std::size_t length = load_be<std::uint16_t>(file_.data() + offset) + std::size_t{2};
    if (bytes + length > max_payload_) {
      break;
    }
    bytes += length;
    ++count;
  }
  // The publisher may have overwritten these slots while they were read; if so, give up cleanly.
  if (request.seq < window_.oldest()) {
    refused_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  std::array<std::byte, mold::kHeaderBytes> header{};
  mold::encode(header.data(), session_, request.seq, count);
  socket_.send(to, header, file_.subspan(first, bytes));
}

}  // namespace itch
