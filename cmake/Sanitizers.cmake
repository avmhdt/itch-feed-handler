# Sanitizer and coverage flags, selected by the ITCH_SANITIZE cache variable.
set(ITCH_SANITIZE "" CACHE STRING "One of: address, thread, fuzzer, coverage, or empty")

if(ITCH_SANITIZE STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
elseif(ITCH_SANITIZE STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
elseif(ITCH_SANITIZE STREQUAL "fuzzer")
  add_compile_options(-fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all
                      -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
elseif(ITCH_SANITIZE STREQUAL "coverage")
  add_compile_options(--coverage -fprofile-update=atomic)
  add_link_options(--coverage)
elseif(NOT ITCH_SANITIZE STREQUAL "")
  message(FATAL_ERROR "Unknown ITCH_SANITIZE value: ${ITCH_SANITIZE}")
endif()
