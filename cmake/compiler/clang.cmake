
function(add_sanitizers)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endfunction()

# This warning has a false positive. See
# <https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108088>.
add_compile_options(-Wno-error=free-nonheap-object)
add_compile_options(-Wall -Wextra -pedantic) #-Werror)

# This warning has a false positive with clang. See
# <https://stackoverflow.com/questions/52416362>.
add_compile_options(-Wno-error=unused-lambda-capture)

# If we're building with clang, then use the libc++ version of the standard
# library instead of libstdc++. Better coverage of build configurations.
#
# But there's one exception: libfuzzer is built with libstdc++ on Ubuntu,
# and so won't link to libc++. So, if any of the FUZZ_* variables are set,
# keep to libstdc++ (the default on most systems).
if (NOT ${FUZZ_W3C_PROPAGATION})
  add_compile_options(-stdlib=libc++)
  add_link_options(-stdlib=libc++)
endif ()

if (BUILD_COVERAGE)
  set(COVERAGE_LIBRARIES gcov)
  add_compile_options(-g -O0 -fprofile-arcs -ftest-coverage)
endif()

if(FUZZ_W3C_PROPAGATION)
  add_compile_options(-fsanitize=fuzzer)
  add_link_options(-fsanitize=fuzzer)
  add_sanitizers()
endif()

if (SANITIZE)
  add_sanitizers()
endif()
