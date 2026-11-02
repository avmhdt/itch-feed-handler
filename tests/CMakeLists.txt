CPMAddPackage(NAME googletest GITHUB_REPOSITORY google/googletest GIT_TAG v1.15.2 SYSTEM YES
              OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt ON")
include(GoogleTest)

add_executable(itch_unit_tests
  unit/frame_reader_test.cpp
  unit/mapped_file_test.cpp
)
target_link_libraries(itch_unit_tests PRIVATE itch_core GTest::gtest_main)
itch_set_warnings(itch_unit_tests)
gtest_discover_tests(itch_unit_tests DISCOVERY_MODE PRE_TEST)

add_test(NAME cli_stats_raw_types
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/stats_raw_types.sh $<TARGET_FILE:itch>)
