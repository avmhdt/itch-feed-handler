#include "itch/net/drop_plan.hpp"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace itch {

namespace {

constexpr double kTwoToMinus64 = 1.0 / 18446744073709551616.0;

/// SplitMix64: small, seedable, and the same on every platform.
std::uint64_t split_mix(std::uint64_t& state) noexcept {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

bool parse_number(std::string_view text, std::uint64_t& out) {
  const char* end = text.data() + text.size();
  const auto result = std::from_chars(text.data(), end, out);
  return result.ec == std::errc{} && result.ptr == end && !text.empty();
}

}  // namespace

DropPlan::DropPlan(std::uint64_t seed, double random_rate, std::vector<Burst> bursts,
                   std::set<std::uint64_t> sequences)
    : state_(seed),
      random_rate_(random_rate),
      bursts_(std::move(bursts)),
      sequences_(std::move(sequences)) {
  std::sort(bursts_.begin(), bursts_.end(),
            [](const Burst& a, const Burst& b) { return a.at_seq < b.at_seq; });
}

DropPlan::Burst DropPlan::parse_burst(const std::string& text) {
  const std::size_t colon = text.find(':');
  Burst burst;
  if (colon == std::string::npos ||
      !parse_number(std::string_view(text).substr(0, colon), burst.at_seq) ||
      !parse_number(std::string_view(text).substr(colon + 1), burst.packets)) {
    throw std::invalid_argument("expected SEQ:COUNT, got " + text);
  }
  return burst;
}

double DropPlan::next_uniform() noexcept {
  return static_cast<double>(split_mix(state_)) * kTwoToMinus64;
}

bool DropPlan::should_drop(std::uint64_t first_seq, std::uint64_t count) noexcept {
  // Draw for every packet, so that a burst or list does not shift the random pattern.
  bool drop = random_rate_ > 0.0 && next_uniform() < random_rate_;
  if (next_burst_ < bursts_.size() && first_seq + count > bursts_[next_burst_].at_seq) {
    burst_left_ += bursts_[next_burst_].packets;
    ++next_burst_;
  }
  if (burst_left_ > 0) {
    --burst_left_;
    drop = true;
  }
  if (!sequences_.empty()) {
    const auto hit = sequences_.lower_bound(first_seq);
    drop = drop || (hit != sequences_.end() && *hit < first_seq + count);
  }
  dropped_ += drop ? 1 : 0;
  return drop;
}

}  // namespace itch
