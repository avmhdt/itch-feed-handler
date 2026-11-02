#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "commands.hpp"
#include "common.hpp"

namespace itch::cli {

namespace {

struct DigestOptions {
  BookRunOptions run;
  std::string at;
};

}  // namespace

void register_digest(CLI::App& app, int& exit_code) {
  auto* command = app.add_subcommand("digest", "Deterministic hash of all books and counters");
  auto options = std::make_shared<DigestOptions>();
  command->add_option("--file", options->run.path, "Uncompressed ITCH file")->required();
  command->add_option("--at", options->at, "Eastern time, HH:MM:SS[.fraction]; default end of day");
  add_book_options(*command, options->run);

  command->callback([options, &exit_code] {
    if (!options->at.empty()) {
      options->run.until_timestamp_ns = parse_time_of_day(options->at);
    }
    const BookRun run = run_books(options->run);
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx",
                  static_cast<unsigned long long>(run.manager->digest()));
    emit("", text);
    exit_code = finish_run(run.result);
  });
}

}  // namespace itch::cli
