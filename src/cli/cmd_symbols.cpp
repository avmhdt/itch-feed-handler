#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "commands.hpp"
#include "common.hpp"
#include "itch/io/file_driver.hpp"
#include "itch/io/mapped_file.hpp"
#include "itch/state/instruments.hpp"

namespace itch::cli {

namespace {

/// Reads Stock Directory messages only; directory entries can appear at any point in the day.
class DirectoryReader {
 public:
  void on(const StockDirectory& message) noexcept { table_.define(message); }
  template <class View>
  void on(const View&) noexcept {}

  [[nodiscard]] const LocateTable& table() const noexcept { return table_; }

 private:
  LocateTable table_;
};

std::string directory_json(const LocateTable& table) {
  std::ostringstream json;
  json << '[';
  bool first = true;
  for (const Instrument* instrument : table.all()) {
    json << (first ? "" : ",") << "{\"etp\":\"" << instrument->etp_flag
         << "\",\"locate\":" << instrument->locate << ",\"market_category\":\""
         << instrument->market_category << "\",\"round_lot_size\":" << instrument->round_lot_size
         << ",\"symbol\":\"" << instrument->symbol() << "\"}";
    first = false;
  }
  json << ']';
  return json.str();
}

std::string directory_table(const LocateTable& table) {
  std::ostringstream text;
  text << "LOCATE SYMBOL   CAT ROUND_LOT\n";
  for (const Instrument* instrument : table.all()) {
    text << instrument->locate << ' ' << instrument->symbol() << ' ' << instrument->market_category
         << ' ' << instrument->round_lot_size << '\n';
  }
  return text.str();
}

struct SymbolsOptions {
  std::string path;
  std::string json_out;
};

}  // namespace

void register_symbols(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("symbols", "Stock directory of a file");
  auto options = std::make_shared<SymbolsOptions>();
  command->add_option("--file", options->path, "Uncompressed ITCH file")->required();
  command->add_option("--json", options->json_out, "Write the JSON report here");

  command->callback([options, &exit_code] {
    const MappedFile file(options->path);
    auto reader = std::make_unique<DirectoryReader>();
    const DriveResult result =
        drive(file.bytes(), *reader, {}, [](const DriveResult&, std::uint64_t) {});
    if (options->json_out.empty()) {
      emit("", directory_table(reader->table()));
    } else {
      emit(options->json_out, directory_json(reader->table()));
    }
    exit_code = finish_run(result);
  });
}

}  // namespace itch::cli
