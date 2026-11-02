#include <exception>
#include <iostream>

#include "commands.hpp"

int main(int argc, char** argv) {
  CLI::App app{"Nasdaq TotalView-ITCH 5.0 feed handler"};
  app.require_subcommand(1);

  int exit_code = itch::cli::kExitOk;
  itch::cli::register_stats(app, exit_code);

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
