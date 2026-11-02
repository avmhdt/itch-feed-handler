#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace itch {

/// Day totals for one symbol. All are integer shares (NFR-05). `volume` is DQ-09 Nasdaq-venue
/// volume before broken trades are taken out.
struct SymbolCounters {
  std::int64_t added = 0;  // Shares from A, F and the new side of U.
  std::int64_t executed = 0;
  std::int64_t cancelled = 0;
  std::int64_t deleted = 0;
  std::int64_t replaced_out = 0;
  std::int64_t volume = 0;
  std::int64_t cross_volume = 0;
  std::int64_t crossed_outside = 0;
  std::int64_t locked_outside = 0;
};

/// Where a crossed book was seen, kept for the report (DQ-12, R-10).
struct CrossedSample {
  std::uint64_t seq = 0;
  std::uint16_t locate = 0;
};

/// Day counters (DQ-03 to DQ-06 and DQ-12). Every one of these except the in-auction and locked
/// counts is expected to be zero; a non-zero value is investigated, never filtered out.
struct Counters {
  static constexpr std::size_t kMaxSamples = 32;

  std::uint64_t undefined_locates = 0;  // DQ-03
  std::uint64_t duplicate_adds = 0;     // DQ-04
  std::uint64_t orphans = 0;            // DQ-05
  std::uint64_t over_executions = 0;    // DQ-06
  std::uint64_t timestamp_regressions = 0;
  std::uint64_t crossed_outside_auction = 0;
  std::uint64_t crossed_in_auction = 0;
  std::uint64_t locked_outside_auction = 0;
  std::uint64_t locked_in_auction = 0;
  std::uint64_t broken_trades = 0;
  std::array<CrossedSample, kMaxSamples> crossed_samples{};
  std::size_t crossed_sample_count = 0;
};

}  // namespace itch
