#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "itch/analytics/bars.hpp"
#include "itch/out/schemas.hpp"
#include "itch/state/instruments.hpp"
#include "itch/util/spsc_queue.hpp"
#include "itch/util/time.hpp"

namespace itch {

/// The eight datasets of spec section 7(b).
enum class Dataset : unsigned {
  BookL3 = 1U << 0U,
  Trades = 1U << 1U,
  Bbo = 1U << 2U,
  Bars = 1U << 3U,
  Snapshots = 1U << 4U,
  TradingState = 1U << 5U,
  Noii = 1U << 6U,
  Instruments = 1U << 7U,
};

[[nodiscard]] unsigned dataset_mask(const std::vector<std::string>& names);

struct SinkOptions {
  std::filesystem::path root;  ///< Empty keeps trades in memory and writes no files.
  std::string venue = "XNAS";
  CivilDate date;
  bool all_symbols = false;
  std::vector<std::string> symbols;  ///< Output symbols when `all_symbols` is off.
  unsigned datasets = 0xFFU;
  int compression_level = 3;
  std::int64_t row_group_rows = 1'000'000;
  std::size_t queue_capacity = 65'536;
  std::vector<std::uint64_t> bar_intervals_ns{1'000'000'000ULL, 60'000'000'000ULL};
  bool bars_rth_only = false;
  bool bars_include_cross = true;
  std::uint64_t snapshot_interval_ns = 60'000'000'000ULL;
  std::size_t snapshot_depth = 10;
};

/// What the correction pass found for one symbol.
struct SymbolVolume {
  std::int64_t volume = 0;  ///< DQ-09 volume from trades, broken trades excluded.
  std::int64_t broken_shares = 0;
  std::int64_t closing_cross_shares = 0;
  std::int64_t closing_cross_ticks = 0;
};

/// Hands records from the hot path to a writer thread over one bounded SPSC queue per dataset, and
/// writes partitioned Parquet (FR-13 to FR-17). Trades stay in memory until the end of the day,
/// because a Broken Trade message can arrive at any time and Parquet files cannot be edited: the
/// correction pass marks them, then writes trades and derives bars from the corrected rows.
class Sinks {
 public:
  explicit Sinks(SinkOptions options);
  ~Sinks();

  Sinks(const Sinks&) = delete;
  Sinks& operator=(const Sinks&) = delete;

  /// Called on each Stock Directory message.
  void define(const Instrument& instrument);

  [[nodiscard]] bool wants(std::uint16_t locate) const noexcept { return wanted_[locate] != 0; }
  [[nodiscard]] bool enabled(Dataset dataset) const noexcept {
    return (options_.datasets & static_cast<unsigned>(dataset)) != 0;
  }
  [[nodiscard]] const std::vector<std::uint16_t>& wanted_locates() const noexcept {
    return wanted_locates_;
  }
  [[nodiscard]] const TradingDay& day() const noexcept { return day_; }
  [[nodiscard]] const SinkOptions& options() const noexcept { return options_; }

  void push(const L3Record& record) noexcept { push_to(l3_, record); }
  void push(const TradeRecord& record) noexcept { push_to(trades_, record); }
  void push(const BboRecord& record) noexcept { push_to(bbo_, record); }
  void push(const SnapshotRecord& record) noexcept { push_to(snapshots_, record); }
  void push(const StateRecord& record) noexcept { push_to(state_, record); }
  void push(const NoiiRecord& record) noexcept { push_to(noii_, record); }

  /// Ends the day: drains the queues, applies `broken_matches`, and writes what remains.
  void finish(const std::vector<std::uint64_t>& broken_matches, const LocateTable& instruments);

  /// Corrected trades of a symbol; valid after finish().
  [[nodiscard]] std::span<const TradeRecord> trades(std::uint16_t locate) const noexcept;
  [[nodiscard]] SymbolVolume volume(std::uint16_t locate) const noexcept;

 private:
  class Writer;

  template <class T>
  static void push_to(SpscQueue<T>& queue, const T& record) noexcept {
    while (!queue.try_push(record)) {
      std::this_thread::yield();
    }
  }

  void run_writer();

  SinkOptions options_;
  TradingDay day_;
  std::vector<std::uint8_t> wanted_;
  std::vector<std::uint16_t> wanted_locates_;
  std::vector<std::string> wanted_symbols_;
  SpscQueue<L3Record> l3_;
  SpscQueue<TradeRecord> trades_;
  SpscQueue<BboRecord> bbo_;
  SpscQueue<SnapshotRecord> snapshots_;
  SpscQueue<StateRecord> state_;
  SpscQueue<NoiiRecord> noii_;
  std::unique_ptr<Writer> writer_;
  std::thread thread_;
  std::atomic<bool> finishing_{false};
};

/// Writes bars for one interval to a single Parquet file, for `itch bars --out`.
void write_bars_file(const std::filesystem::path& path, std::span<const BarRecord> bars,
                     const std::string& venue, const std::string& symbol,
                     const std::string& interval);

}  // namespace itch
