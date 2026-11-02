add_executable(fuzz_parser fuzz_parser.cpp)
target_link_libraries(fuzz_parser PRIVATE itch_core)
target_link_options(fuzz_parser PRIVATE -fsanitize=fuzzer)
itch_set_warnings(fuzz_parser)

add_executable(fuzz_book fuzz_book.cpp)
target_link_libraries(fuzz_book PRIVATE itch_core)
target_link_options(fuzz_book PRIVATE -fsanitize=fuzzer)
itch_set_warnings(fuzz_book)
