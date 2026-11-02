add_library(itch_core STATIC
  io/mapped_file.cpp
  state/instruments.cpp
  util/time.cpp
)
target_include_directories(itch_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(itch_core PUBLIC cxx_std_20)
itch_set_warnings(itch_core)

add_executable(itch
  cli/main.cpp
  cli/cmd_stats.cpp
  cli/cmd_symbols.cpp
  cli/common.cpp
)
target_link_libraries(itch PRIVATE itch_core CLI11::CLI11 spdlog::spdlog)
itch_set_warnings(itch)
