#include "itch/util/config.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <toml++/toml.hpp>

namespace itch {

namespace {

template <class T>
T get(toml::node_view<const toml::node> node, T fallback) {
  return node.value<T>().value_or(std::move(fallback));
}

std::string env_string(toml::node_view<const toml::node> node, std::string fallback) {
  const auto value = node.value<std::string>();
  return value ? expand_env(*value) : std::move(fallback);
}

std::vector<std::string> string_list(toml::node_view<const toml::node> node,
                                     std::vector<std::string> fallback) {
  const toml::array* items = node.as_array();
  if (items == nullptr) {
    return fallback;
  }
  std::vector<std::string> out;
  for (const toml::node& item : *items) {
    if (const auto text = item.value<std::string>()) {
      out.push_back(*text);
    }
  }
  return out;
}

}  // namespace

std::string expand_env(std::string_view text) {
  std::string out;
  std::size_t position = 0;
  while (position < text.size()) {
    const std::size_t open = text.find("${", position);
    if (open == std::string_view::npos) {
      out.append(text.substr(position));
      break;
    }
    const std::size_t close = text.find('}', open);
    if (close == std::string_view::npos) {
      throw std::runtime_error("unterminated ${ in: " + std::string(text));
    }
    out.append(text.substr(position, open - position));
    const std::string name(text.substr(open + 2, close - open - 2));
    const char* value = std::getenv(name.c_str());
    if (value == nullptr) {
      throw std::runtime_error("environment variable not set: " + name);
    }
    out.append(value);
    position = close + 1;
  }
  return out;
}

Config load_config(const std::filesystem::path& path) {
  toml::table table;
  try {
    table = toml::parse_file(path.string());
  } catch (const toml::parse_error& error) {
    throw std::runtime_error(path.string() + ": " + std::string(error.description()));
  }

  Config config;
  const toml::table& root = table;
  const auto section = [&root](const char* name) { return root[name]; };
  const auto integer = [](toml::node_view<const toml::node> node, auto fallback) {
    return static_cast<decltype(fallback)>(get<std::int64_t>(node, std::int64_t(fallback)));
  };

  config.input_path = env_string(section("input")["path"], "");
  config.input_date = get<std::string>(section("input")["date"], "");
  config.strict = get<bool>(section("input")["strict"], true);
  config.watchlist_file = env_string(section("symbols")["watchlist_file"], config.watchlist_file);
  config.outputs_for = get<std::string>(section("symbols")["outputs_for"], config.outputs_for);
  config.outputs_root = env_string(section("outputs")["root"], "data");
  config.venue = get<std::string>(section("outputs")["venue"], config.venue);
  config.datasets = string_list(section("outputs")["datasets"], {});
  config.compression_level = integer(section("outputs")["compression_level"], 3);
  config.row_group_rows = integer(section("outputs")["row_group_rows"], config.row_group_rows);
  config.queue_capacity = integer(section("outputs")["queue_capacity"], config.queue_capacity);
  config.bar_intervals = string_list(section("bars")["intervals"], config.bar_intervals);
  config.bar_session = get<std::string>(section("bars")["session"], config.bar_session);
  config.bar_include_cross = get<bool>(section("bars")["include_cross"], true);
  config.snapshot_interval =
      get<std::string>(section("snapshots")["interval"], config.snapshot_interval);
  config.snapshot_depth = integer(section("snapshots")["depth"], config.snapshot_depth);
  config.crossed_book = get<std::string>(section("checks")["crossed_book"], config.crossed_book);
  config.order_capacity_hint =
      integer(section("book")["order_capacity_hint"], config.order_capacity_hint);
  config.order_map = get<std::string>(section("book")["order_map"], config.order_map);
  return config;
}

std::vector<std::string> read_watchlist(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("cannot read watchlist: " + path.string());
  }
  std::vector<std::string> symbols;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line[0] != '#') {
      symbols.push_back(line);
    }
  }
  return symbols;
}

}  // namespace itch
