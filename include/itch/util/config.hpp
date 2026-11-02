#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace itch {

/// Replaces each ${VAR} with its value from the environment; an unset variable is an error.
[[nodiscard]] std::string expand_env(std::string_view text);

/// The subset of configs/default.toml that `itch build` reads; see spec section 10.
struct Config {
  std::string input_path;
  std::string input_date;
  bool strict = true;
  std::string watchlist_file = "configs/watchlist.txt";
  std::string outputs_for = "watchlist";
  std::filesystem::path outputs_root = "data";
  std::string venue = "XNAS";
  std::vector<std::string> datasets;
  int compression_level = 3;
  std::int64_t row_group_rows = 1'000'000;
  std::size_t queue_capacity = 65'536;
  std::vector<std::string> bar_intervals{"1s", "1m"};
  std::string bar_session = "all";
  bool bar_include_cross = true;
  std::string snapshot_interval = "1m";
  std::size_t snapshot_depth = 10;
  std::string crossed_book = "count";
  std::size_t order_capacity_hint = 50'000'000;
  std::string order_map = "open_addressing";
};

/// Reads a TOML config, expanding ${VAR} in string values. Throws std::runtime_error on a file
/// that cannot be read or parsed.
[[nodiscard]] Config load_config(const std::filesystem::path& path);

/// Reads a one-symbol-per-line file; blank lines and # comments are skipped.
[[nodiscard]] std::vector<std::string> read_watchlist(const std::filesystem::path& path);

}  // namespace itch
