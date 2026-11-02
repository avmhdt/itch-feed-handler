#include <exception>
#include <iostream>

#include "commands.hpp"
#include "common.hpp"

int main(int argc, char** argv) {
  CLI::App app{"Nasdaq TotalView-ITCH 5.0 feed handler"};
  app.require_subcommand(1);
  itch::cli::init_logging();

  int exit_code = itch::cli::kExitOk;
  itch::cli::register_stats(app, exit_code);
  itch::cli::register_symbols(app, exit_code);
  itch::cli::register_book_at(app, exit_code);
  itch::cli::register_check(app, exit_code);
  itch::cli::register_digest(app, exit_code);
  itch::cli::register_build(app, exit_code);
  itch::cli::register_bars(app, exit_code);

  try {
    app.parse(argc, argv);
  } catch (const CLI::Success&) {
    app.exit(CLI::Success{});
    return itch::cli::kExitOk;
  } catch (const CLI::ParseError& error) {
    app.exit(error);
    return itch::cli::kExitUsage;
  } catch (const std::exception& error) {
    std::cerr << "itch: " << error.what() << '\n';
    return itch::cli::kExitUsage;
  }
  return exit_code;
}
