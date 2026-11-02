#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "commands.hpp"
#include "common.hpp"
#include "itch/io/mapped_file.hpp"
#include "itch/net/mold_publisher.hpp"

namespace itch::cli {

namespace {

struct ReplayOptions {
  std::string path;
  std::string group = "239.192.0.1:30001";
  std::string iface = "127.0.0.1";
  std::uint16_t retrans_port = 30002;
  std::string rate = "max";
  std::string session;
  double drop_rate = 0.0;
  std::vector<std::string> drop_bursts;
  std::string drop_seqs;
  std::uint64_t seed = 42;
  std::uint64_t max_messages_per_s = 0;
  std::size_t retrans_buffer = 2'000'000;
  std::uint64_t limit = 0;
  int linger_ms = 3000;
};

/// "max", or a multiple of real time such as "1x" or "10x".
double parse_rate(const std::string& rate) {
  if (rate == "max") {
    return 0.0;
  }
  char* end = nullptr;
  const double speed = std::strtod(rate.c_str(), &end);
  if (speed <= 0.0 || end == nullptr || std::string(end) != "x") {
    throw CLI::ValidationError("--rate", "expected max or a multiple such as 10x, got " + rate);
  }
  return speed;
}

std::set<std::uint64_t> parse_sequences(const std::string& text) {
  std::set<std::uint64_t> sequences;
  std::stringstream stream(text);
  std::string item;
  while (std::getline(stream, item, ',')) {
    sequences.insert(std::stoull(item));
  }
  return sequences;
}

}  // namespace

void register_replay(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("replay", "Publish a file as MoldUDP64 over local UDP");
  auto options = std::make_shared<ReplayOptions>();
  command->add_option("--file", options->path, "Uncompressed ITCH file")->required();
  command->add_option("--group", options->group, "Multicast group, ADDRESS:PORT");
  command->add_option("--iface", options->iface, "Interface address; loopback only");
  command->add_option("--retrans-port", options->retrans_port, "Unicast re-request port");
  command->add_option("--rate", options->rate, "max, or a multiple of real time such as 10x");
  command->add_option("--session", options->session, "Ten-character session id");
  command->add_option("--drop-rate", options->drop_rate, "Random packet drop probability");
  command->add_option("--drop-burst", options->drop_bursts, "Burst to drop, SEQ:COUNT");
  command->add_option("--drop-seqs", options->drop_seqs, "Sequences to drop, comma separated");
  command->add_option("--seed", options->seed, "Seed for random drops");
  command->add_option("--max-msgs-per-s", options->max_messages_per_s, "Cap for --rate max");
  command->add_option("--retrans-buffer", options->retrans_buffer, "Messages held for re-requests");
  command->add_option("--limit", options->limit, "Stop after this many messages");
  command->add_option("--linger-ms", options->linger_ms, "How long end of session persists");

  command->callback([options, &exit_code] {
    PublisherOptions publisher;
    publisher.group = parse_endpoint(options->group);
    publisher.iface = options->iface;
    publisher.retrans_port = options->retrans_port;
    publisher.speed = parse_rate(options->rate);
    publisher.max_messages_per_s = options->max_messages_per_s;
    publisher.retrans_buffer_msgs = options->retrans_buffer;
    publisher.limit = options->limit;
    publisher.linger_ms = options->linger_ms;
    const char* env_session = std::getenv("ITCH_MOLD_SESSION");
    publisher.session = mold::make_session(
        !options->session.empty() ? options->session
                                  : (env_session != nullptr ? env_session : "ITCHDEMO01"));

    std::vector<DropPlan::Burst> bursts;
    for (const std::string& burst : options->drop_bursts) {
      bursts.push_back(DropPlan::parse_burst(burst));
    }
    DropPlan plan(options->seed, options->drop_rate, std::move(bursts),
                  parse_sequences(options->drop_seqs));

    const MappedFile file(options->path);
    const PublisherStats stats = publish(file.bytes(), publisher, plan);
    spdlog::info("published {} messages in {} packets, dropped {}, {} re-requests, {} refused",
                 stats.messages, stats.packets, stats.dropped_packets, stats.retrans_requests,
                 stats.retrans_refused);
    exit_code = kExitOk;
  });
}

}  // namespace itch::cli
