# NFR-06: warnings are errors on both compilers.
function(itch_set_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endfunction()
