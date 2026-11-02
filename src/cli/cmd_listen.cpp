#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>

#include "commands.hpp"
#include "common.hpp"
#include "itch/book/book_manager.hpp"
#include "itch/net/mold_listener.hpp"
#include "type_counter.hpp"

namespace itch::cli {

namespace {

struct ListenCliOptions {
  std::string group = "239.192.0.1:30001";
  std::string retrans = "127.0.0.1:30002";
  std::string iface = "127.0.0.1";
  std::string digest_out;
  std::string stats_out;
  std::string date;
  std::size_t order_capacity = std::size_t{1} << 22;
  int rerequest_timeout_ms = 50;
  int rerequest_max_retries = 20;
  int idle_timeout_ms = 30'000;
  bool build_books = false;
};

/// Counts every message and, when asked, applies it to the books too.
class ListenHandler {
 public:
  explicit ListenHandler(BookManager* books) : books_(books) {}

  template <class View>
  void on(const View& message) {
    counter_.on(message);
    if (books_ != nullptr) {
      books_->on(message);
    }
  }

  [[nodiscard]] const TypeCounter& counter() const noexcept { return counter_; }

 private:
  TypeCounter counter_;
  BookManager* books_;
};

std::string stats_json(const ListenHandler& handler, const ListenResult& result,
                       const std::optional<CivilDate>& date, double wall_s) {
  const ReceiverStats& stats = result.stats;
  std::ostringstream json;
  json << "{\"counts\":"
       << counts_json(handler.counter(), stats.messages, result.malformed_messages, 0, date)
       << ",\"duplicate_packets\":" << stats.duplicate_packets
       << ",\"gap_messages\":" << stats.gap_messages << ",\"gaps\":" << stats.gaps
       << ",\"heartbeats\":" << stats.heartbeats << ",\"largest_gap\":" << stats.largest_gap
       << ",\"overflow_packets\":" << stats.overflow_packets << ",\"packets\":" << stats.packets
       << ",\"rerequests\":" << stats.rerequests << ",\"timeline\":[";
  bool first = true;
  for (const GapBucket& bucket : stats.timeline) {
    json << (first ? "" : ",") << "{\"gaps\":" << bucket.gaps << ",\"messages\":" << bucket.messages
         << ",\"rerequests\":" << bucket.rerequests << '}';
    first = false;
  }
  json << "],\"timeouts\":" << stats.timeouts
       << ",\"unrecovered\":" << (result.status == MoldReceiver::Status::Finished ? 0 : 1)
       << ",\"wall_s\":" << wall_s << '}';
  return json.str();
}

}  // namespace

void register_listen(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("listen", "Receive MoldUDP64 and recover gaps");
  auto options = std::make_shared<ListenCliOptions>();
  command->add_option("--group", options->group, "Multicast group, ADDRESS:PORT");
  command->add_option("--retrans", options->retrans, "Re-request server, ADDRESS:PORT");
  command->add_option("--iface", options->iface, "Interface address; loopback only");
  command->add_flag("--build-books", options->build_books, "Apply messages to order books");
  command->add_option("--digest-out", options->digest_out, "Write the book digest here");
  command->add_option("--stats-out", options->stats_out, "Write counts and gap statistics here");
  command->add_option("--date", options->date, "Trading date, YYYY-MM-DD");
  command->add_option("--order-capacity", options->order_capacity, "Expected live orders");
  command->add_option("--rerequest-timeout-ms", options->rerequest_timeout_ms, "Re-request timer");
  command->add_option("--rerequest-max-retries", options->rerequest_max_retries, "Retries per gap");
  command->add_option("--idle-timeout-ms", options->idle_timeout_ms, "Give up after this silence");

  command->callback([options, &exit_code] {
    ListenOptions listen_options;
    listen_options.group = parse_endpoint(options->group);
    listen_options.retrans = parse_endpoint(options->retrans);
    listen_options.iface = options->iface;
    listen_options.receiver.rerequest_timeout_ns = options->rerequest_timeout_ms * 1'000'000LL;
    listen_options.receiver.rerequest_max_retries = options->rerequest_max_retries;
    listen_options.idle_timeout_ms = options->idle_timeout_ms;

    std::unique_ptr<BookManager> books;
    if (options->build_books) {
      books =
          std::make_unique<BookManager>(BookConfig{.order_capacity_hint = options->order_capacity});
    }
    ListenHandler handler(books.get());
    const auto start = std::chrono::steady_clock::now();
    const ListenResult result = listen(listen_options, handler);
    const std::chrono::duration<double> wall = std::chrono::steady_clock::now() - start;

    spdlog::info("received {} messages in {} packets: {} gaps, {} re-requests, {} duplicates",
                 result.stats.messages, result.stats.packets, result.stats.gaps,
                 result.stats.rerequests, result.stats.duplicate_packets);
    std::optional<CivilDate> date;
    if (!options->date.empty()) {
      date = parse_date(options->date);
    }
    if (!options->stats_out.empty()) {
      emit(options->stats_out, stats_json(handler, result, date, wall.count()));
    }
    if (result.status != MoldReceiver::Status::Finished) {
      if (result.idle_timeout) {
        spdlog::error("no data for {} ms; last delivered sequence {}", options->idle_timeout_ms,
                      result.expected - 1);
      } else {
        spdlog::error(
            "unrecoverable gap at sequence {}: not retransmitted after {} retries, so it "
            "is older than the retransmission window",
            result.expected, options->rerequest_max_retries);
      }
      exit_code = kExitUnrecoverableGap;
      return;
    }
    if (books && !options->digest_out.empty()) {
      char text[24];
      std::snprintf(text, sizeof(text), "%016llx",
                    static_cast<unsigned long long>(books->digest()));
      emit(options->digest_out, text);
    }
    exit_code = kExitOk;
  });
}

}  // namespace itch::cli
