#pragma once

#include <CLI/CLI.hpp>

namespace itch::cli {

/// Process exit codes (spec section 10).
inline constexpr int kExitOk = 0;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitMalformed = 3;

/// Registers a subcommand; `exit_code` receives the result of the command when it runs.
void register_stats(CLI::App& app, int& exit_code);

}  // namespace itch::cli
