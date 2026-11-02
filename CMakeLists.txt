cmake_minimum_required(VERSION 3.28)
project(itch-feed-handler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(ITCH_BUILD_TESTS "Build unit, property and integration tests" ON)
option(ITCH_BUILD_FUZZ "Build libFuzzer harnesses (Clang only)" OFF)
option(ITCH_BUILD_BENCH "Build Google Benchmark micro suite" OFF)

if(NOT DEFINED CPM_SOURCE_CACHE AND DEFINED ENV{HOME})
  set(CPM_SOURCE_CACHE "$ENV{HOME}/.cache/CPM")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CPM)
include(Warnings)
include(Sanitizers)

CPMAddPackage(NAME CLI11 GITHUB_REPOSITORY CLIUtils/CLI11 VERSION 2.4.2 SYSTEM YES
              OPTIONS "CLI11_BUILD_TESTS OFF" "CLI11_BUILD_EXAMPLES OFF")
CPMAddPackage(NAME spdlog GITHUB_REPOSITORY gabime/spdlog VERSION 1.14.1 SYSTEM YES)
CPMAddPackage(NAME tomlplusplus GITHUB_REPOSITORY marzer/tomlplusplus VERSION 3.4.0 SYSTEM YES)
CPMAddPackage(NAME unordered_dense GITHUB_REPOSITORY martinus/unordered_dense VERSION 4.4.0
              SYSTEM YES)

find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)

add_subdirectory(src)

if(ITCH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(ITCH_BUILD_FUZZ)
  add_subdirectory(fuzz)
endif()

if(ITCH_BUILD_BENCH)
  add_subdirectory(bench)
endif()

# D-08: the licence covers the code only; market data is never redistributed.
set(CPACK_PACKAGE_NAME "itch-feed-handler")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
include(CPack)
