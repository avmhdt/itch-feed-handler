CPMAddPackage(NAME googletest GITHUB_REPOSITORY google/googletest GIT_TAG v1.15.2 SYSTEM YES
              OPTIONS "INSTALL_GTEST OFF" "gtest_force_shared_crt ON")
CPMAddPackage(NAME rapidcheck GITHUB_REPOSITORY emil-e/rapidcheck
              GIT_TAG ff6af6fc683159deb51c543b065eba14dfcf329b SYSTEM YES
              OPTIONS "RC_ENABLE_GTEST ON")
include(GoogleTest)

add_executable(itch_unit_tests
  unit/bars_test.cpp
  unit/book_manager_test.cpp
  unit/config_test.cpp
  unit/endian_test.cpp
  unit/frame_reader_test.cpp
  unit/instruments_test.cpp
  unit/mapped_file_test.cpp
  unit/messages_test.cpp
  unit/mold_receiver_test.cpp
  unit/moldudp64_test.cpp
  unit/order_book_replace_test.cpp
  unit/order_book_test.cpp
  unit/order_store_test.cpp
  unit/parse_test.cpp
  unit/time_test.cpp
)
target_link_libraries(itch_unit_tests PRIVATE itch_core GTest::gtest_main)
target_compile_definitions(itch_unit_tests PRIVATE ITCH_REPO_DIR="${PROJECT_SOURCE_DIR}")
itch_set_warnings(itch_unit_tests)
gtest_discover_tests(itch_unit_tests DISCOVERY_MODE PRE_TEST)

set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

add_executable(itch_property_tests property/book_property_test.cpp)
target_link_libraries(itch_property_tests PRIVATE itch_core rapidcheck_gtest GTest::gtest_main)
itch_set_warnings(itch_property_tests)
gtest_discover_tests(itch_property_tests DISCOVERY_MODE PRE_TEST)

# The counting operator new cannot coexist with the sanitizer allocators.
if(ITCH_SANITIZE STREQUAL "" OR ITCH_SANITIZE STREQUAL "coverage")
  add_executable(itch_no_alloc_test unit/no_alloc_test.cpp)
  target_link_libraries(itch_no_alloc_test PRIVATE itch_core GTest::gtest_main)
  target_compile_definitions(itch_no_alloc_test PRIVATE ITCH_FIXTURE_DIR="${FIXTURES}")
  itch_set_warnings(itch_no_alloc_test)
  add_test(NAME no_alloc COMMAND itch_no_alloc_test)
endif()

add_test(NAME cli_stats_raw_types
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/stats_raw_types.sh $<TARGET_FILE:itch>)
add_test(NAME cli_stats_fixture
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/stats_fixture.sh $<TARGET_FILE:itch>
                 ${FIXTURES})
add_test(NAME cli_symbols_fixture
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/symbols_fixture.sh $<TARGET_FILE:itch>
                 ${FIXTURES})
add_test(NAME build_fixture
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/build_fixture.sh $<TARGET_FILE:itch>
                 ${PROJECT_SOURCE_DIR})
add_executable(itch_loopback_test integration/loopback_test.cpp)
target_link_libraries(itch_loopback_test PRIVATE itch_core GTest::gtest_main)
target_compile_definitions(itch_loopback_test PRIVATE ITCH_FIXTURE_DIR="${FIXTURES}")
itch_set_warnings(itch_loopback_test)
add_test(NAME loopback_udp COMMAND itch_loopback_test)
set_tests_properties(loopback_udp PROPERTIES RUN_SERIAL TRUE)

add_test(NAME book_at_fixture
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/book_fixture.sh $<TARGET_FILE:itch>
                 ${FIXTURES})
