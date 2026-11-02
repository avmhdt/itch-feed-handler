#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace itch {

/// Deliberate packet loss for the gap demo (FR-24). Seeded, so a plan drops the same packets on
/// every run. Dropped packets stay available to the retransmission server.
class DropPlan {
 public:
  struct Burst {
    std::uint64_t at_seq = 0;
    std::uint64_t packets = 0;
  };

  DropPlan() = default;
  DropPlan(std::uint64_t seed, double random_rate, std::vector<Burst> bursts,
           std::set<std::uint64_t> sequences);

  /// Parses "SEQ:COUNT"; throws std::invalid_argument otherwise.
  [[nodiscard]] static Burst parse_burst(const std::string& text);

  /// Decides for a packet holding sequences [first_seq, first_seq + count).
  [[nodiscard]] bool should_drop(std::uint64_t first_seq, std::uint64_t count) noexcept;

  [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

 private:
  [[nodiscard]] double next_uniform() noexcept;

  std::uint64_t state_ = 0;
  double random_rate_ = 0.0;
  std::vector<Burst> bursts_;
  std::size_t next_burst_ = 0;
  std::uint64_t burst_left_ = 0;
  std::set<std::uint64_t> sequences_;
  std::uint64_t dropped_ = 0;
};

}  // namespace itch
