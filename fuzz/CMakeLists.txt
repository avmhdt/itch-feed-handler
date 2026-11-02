add_executable(fuzz_parser fuzz_parser.cpp)
target_link_libraries(fuzz_parser PRIVATE itch_core)
target_link_options(fuzz_parser PRIVATE -fsanitize=fuzzer)
itch_set_warnings(fuzz_parser)
