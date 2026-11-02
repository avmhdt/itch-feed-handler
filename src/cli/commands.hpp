#pragma once

#include <CLI/CLI.hpp>

namespace itch::cli {

/// Process exit codes (spec section 10).
inline constexpr int kExitOk = 0;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitMalformed = 3;
inline constexpr int kExitInvariant = 4;

/// Each registers a subcommand; `exit_code` receives the result of the command when it runs.
void register_stats(CLI::App& app, int& exit_code);
void register_symbols(CLI::App& app, int& exit_code);
void register_book_at(CLI::App& app, int& exit_code);
void register_check(CLI::App& app, int& exit_code);
void register_digest(CLI::App& app, int& exit_code);
void register_build(CLI::App& app, int& exit_code);
void register_bars(CLI::App& app, int& exit_code);

}  // namespace itch::cli
