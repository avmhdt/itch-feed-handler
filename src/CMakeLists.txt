add_library(itch_core STATIC
  analytics/bars.cpp
  book/book_manager.cpp
  book/order_book.cpp
  book/order_store.cpp
  io/mapped_file.cpp
  out/parquet_sink.cpp
  state/instruments.cpp
  state/trading_state.cpp
  util/config.cpp
  util/time.cpp
)
target_include_directories(itch_core PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(itch_core PUBLIC cxx_std_20)
target_link_libraries(itch_core
  PRIVATE unordered_dense::unordered_dense tomlplusplus::tomlplusplus
  PRIVATE Arrow::arrow_shared Parquet::parquet_shared)
itch_set_warnings(itch_core)

add_executable(itch
  cli/main.cpp
  cli/cmd_bars.cpp
  cli/cmd_book_at.cpp
  cli/cmd_build.cpp
  cli/cmd_check.cpp
  cli/cmd_digest.cpp
  cli/cmd_stats.cpp
  cli/cmd_symbols.cpp
  cli/common.cpp
)
target_link_libraries(itch PRIVATE itch_core CLI11::CLI11 spdlog::spdlog)
itch_set_warnings(itch)
