// The whole transport over real sockets, in one process so that TSan sees both ends: replay the
// fixture with random drops and a burst, and the receiver must end with the file-mode digest.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <thread>

#include "itch/book/book_manager.hpp"
#include "itch/io/file_driver.hpp"
#include "itch/io/mapped_file.hpp"
#include "itch/net/mold_listener.hpp"
#include "itch/net/mold_publisher.hpp"

namespace {

using itch::BookManager;
using itch::Endpoint;

constexpr itch::BookConfig kBooks{.order_capacity_hint = std::size_t{1} << 16};

/// Multicast needs the loopback interface to accept the group, which some CI kernels refuse, and
/// R-01 allows unicast with identical framing. ITCH_TEST_UNICAST selects that path.
std::string group(std::uint16_t port) {
  const std::string address =
      std::getenv("ITCH_TEST_UNICAST") != nullptr ? "127.0.0.1" : "239.192.0.1";
  return address + ":" + std::to_string(port);
}

std::uint64_t file_digest(std::span<const std::byte> bytes) {
  auto manager = std::make_unique<BookManager>(kBooks);
  itch::drive(bytes, *manager, {}, [](const itch::DriveResult&, std::uint64_t) {});
  return manager->digest();
}

struct Outcome {
  itch::ListenResult listen;
  itch::PublisherStats publish;
  std::uint64_t digest = 0;
};

Outcome run(std::span<const std::byte> bytes, std::uint16_t port, itch::DropPlan plan,
            std::size_t window) {
  itch::ListenOptions listen_options;
  listen_options.group = itch::parse_endpoint(group(port));
  listen_options.retrans = itch::parse_endpoint("127.0.0.1:" + std::to_string(port + 1));
  listen_options.ring_packets = 4096;
  listen_options.idle_timeout_ms = 20'000;
  listen_options.receiver.rerequest_timeout_ns = 20'000'000;
  listen_options.receiver.rerequest_max_retries = 5;

  itch::PublisherOptions publisher;
  publisher.group = listen_options.group;
  publisher.retrans_port = static_cast<std::uint16_t>(port + 1);
  publisher.max_messages_per_s = 200'000;
  publisher.retrans_buffer_msgs = window;
  publisher.linger_ms = 500;

  auto manager = std::make_unique<BookManager>(kBooks);
  Outcome result;
  std::thread sender([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    result.publish = itch::publish(bytes, publisher, plan);
  });
  result.listen = itch::listen(listen_options, *manager);
  sender.join();
  result.digest = manager->digest();
  return result;
}

class LoopbackTest : public ::testing::Test {
 protected:
  itch::MappedFile file_{std::filesystem::path(ITCH_FIXTURE_DIR) / "mini_day.itchbin"};
};

TEST_F(LoopbackTest, DropsAndABurstRecoverToTheFileDigest) {
  const auto bytes = file_.bytes();
  itch::DropPlan plan(42, 0.05, {{.at_seq = 5'000, .packets = 40}}, {1, 2, 3});
  const Outcome result = run(bytes, 30101, std::move(plan), 100'000);

  EXPECT_EQ(result.listen.status, itch::MoldReceiver::Status::Finished);
  EXPECT_GT(result.publish.dropped_packets, 40U);
  EXPECT_GT(result.listen.stats.gaps, 0U);
  EXPECT_GT(result.listen.stats.rerequests, 0U);
  EXPECT_EQ(result.listen.stats.messages, result.publish.messages);
  EXPECT_EQ(result.digest, file_digest(bytes));
}

TEST_F(LoopbackTest, GapOlderThanTheWindowIsUnrecoverable) {
  // The window holds 50 messages, and the first 2,000 are gone before anything is delivered.
  itch::DropPlan plan(42, 0.0, {{.at_seq = 1, .packets = 200}}, {});
  const Outcome result = run(file_.bytes(), 30111, std::move(plan), 50);
  EXPECT_EQ(result.listen.status, itch::MoldReceiver::Status::Unrecoverable);
}

}  // namespace
