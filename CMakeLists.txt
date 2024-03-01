# Note: Make sure that this version is the same as that in
# "./CheckRequiredCMakeVersion.cmake".
cmake_minimum_required(VERSION 3.24)

project(dd-trace-cpp)

option(BUILD_COVERAGE "Build code with code coverage profiling instrumentation" OFF)
option(BUILD_EXAMPLES "Build example programs" OFF)
option(BUILD_TESTING "Build the unit tests (test/)" OFF)
option(BUILD_FUZZERS "Build fuzzers" OFF)
option(BUILD_BENCHMARK "Build benchmark binaries" OFF)
option(SANITIZE "Build with address sanitizer and undefined behavior sanitizer" OFF)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# This warning has a false positive. See
# <https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108088>.
add_compile_options(-Wno-error=free-nonheap-object)

# This warning has a false positive with clang. See
# <https://stackoverflow.com/questions/52416362>.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wno-error=unused-lambda-capture)

    # If we're building with clang, then use the libc++ version of the standard
    # library instead of libstdc++. Better coverage of build configurations.
    #
    # But there's one exception: libfuzzer is built with libstdc++ on Ubuntu,
    # and so won't link to libc++. So, if any of the FUZZ_* variables are set,
    # keep to libstdc++ (the default on most systems).
    if (NOT ${BUILD_FUZZERS})
      add_compile_options(-stdlib=libc++)
      add_link_options(-stdlib=libc++)
    endif ()
endif()

function(add_sanitizers)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endfunction()

if(BUILD_FUZZERS)
  set(BUILD_TESTING OFF)
  add_compile_options(-fsanitize=fuzzer)
  add_link_options(-fsanitize=fuzzer)
  add_sanitizers()
  add_subdirectory(fuzz)
endif()

if (SANITIZE)
  add_sanitizers()
endif()

if (BUILD_TESTING)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

include(cmake/curl.cmake)

if (APPLE)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
  find_library(SYSTEMCONFIGURATION_LIBRARY SystemConfiguration)
endif ()

if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
    if (BUILD_COVERAGE)
      add_compile_options(-g -O0 -fprofile-arcs -ftest-coverage)
    endif()
endif()

if(BUILD_COVERAGE)
  set(COVERAGE_LIBRARIES gcov)
endif()

add_library(dd_trace_cpp-objects OBJECT)
target_sources(dd_trace_cpp-objects PRIVATE
    src/datadog/base64.cpp
    src/datadog/cerr_logger.cpp
    src/datadog/clock.cpp
    src/datadog/config_manager.cpp
    src/datadog/collector_response.cpp
    src/datadog/curl.cpp
    src/datadog/datadog_agent_config.cpp
    src/datadog/datadog_agent.cpp
    src/datadog/default_http_client_curl.cpp
#     src/datadog/default_http_client_null.cpp use libcurl
    src/datadog/environment.cpp
    src/datadog/error.cpp
    src/datadog/extraction_util.cpp
    src/datadog/glob.cpp
    src/datadog/http_client.cpp
    src/datadog/id_generator.cpp
    src/datadog/limiter.cpp
    src/datadog/logger.cpp
    src/datadog/metrics.cpp
    src/datadog/msgpack.cpp
    src/datadog/null_collector.cpp
    src/datadog/parse_util.cpp
    src/datadog/platform_util.cpp
    src/datadog/propagation_style.cpp
    src/datadog/random.cpp
    src/datadog/rate.cpp
    src/datadog/remote_config.cpp
    src/datadog/runtime_id.cpp
    src/datadog/span.cpp
    src/datadog/span_data.cpp
    src/datadog/span_defaults.cpp
    src/datadog/span_matcher.cpp
    src/datadog/span_sampler_config.cpp
    src/datadog/span_sampler.cpp
    src/datadog/string_util.cpp
    src/datadog/tags.cpp
    src/datadog/tag_propagation.cpp
    src/datadog/threaded_event_scheduler.cpp
    src/datadog/tracer_config.cpp
    src/datadog/tracer_telemetry.cpp
    src/datadog/tracer.cpp
    src/datadog/trace_id.cpp
    src/datadog/trace_sampler_config.cpp
    src/datadog/trace_sampler.cpp
    src/datadog/trace_segment.cpp
    src/datadog/version.cpp
    src/datadog/w3c_propagation.cpp
)

# This library's public headers are just its source headers.
target_sources(dd_trace_cpp-objects PUBLIC
  FILE_SET public_headers
  TYPE HEADERS
  BASE_DIRS src/
  FILES
  src/datadog/base64.h
  src/datadog/config.h
  src/datadog/cerr_logger.h
  src/datadog/clock.h
  src/datadog/config_manager.h
  src/datadog/config_update.h
  src/datadog/collector.h
  src/datadog/collector_response.h
  # src/datadog/curl.h except for curl.h
  src/datadog/datadog_agent_config.h
  src/datadog/datadog_agent.h
  src/datadog/default_http_client.h
  src/datadog/dict_reader.h
  src/datadog/dict_writer.h
  src/datadog/environment.h
  src/datadog/error.h
  src/datadog/event_scheduler.h
  src/datadog/expected.h
  src/datadog/extracted_data.h
  src/datadog/extraction_util.h
  src/datadog/glob.h
  src/datadog/hex.h
  src/datadog/http_client.h
  src/datadog/id_generator.h
  src/datadog/injection_options.h
  src/datadog/json_fwd.hpp
  src/datadog/json.hpp
  src/datadog/limiter.h
  src/datadog/logger.h
  src/datadog/metrics.h
  src/datadog/msgpack.h
  src/datadog/null_collector.h
  src/datadog/optional.h
  src/datadog/parse_util.h
  src/datadog/platform_util.h
  src/datadog/propagation_style.h
  src/datadog/random.h
  src/datadog/rate.h
  src/datadog/remote_config.h
  src/datadog/runtime_id.h
  src/datadog/sampling_decision.h
  src/datadog/sampling_mechanism.h
  src/datadog/sampling_priority.h
  src/datadog/sampling_util.h
  src/datadog/span_config.h
  src/datadog/span_data.h
  src/datadog/span_defaults.h
  src/datadog/span.h
  src/datadog/span_matcher.h
  src/datadog/span_sampler_config.h
  src/datadog/span_sampler.h
  src/datadog/string_util.h
  src/datadog/string_view.h
  src/datadog/tag_propagation.h
  src/datadog/tags.h
  src/datadog/threaded_event_scheduler.h
  src/datadog/tracer_config.h
  src/datadog/tracer_signature.h
  src/datadog/tracer_telemetry.h
  src/datadog/tracer.h
  src/datadog/trace_id.h
  src/datadog/trace_sampler_config.h
  src/datadog/trace_sampler.h
  src/datadog/trace_segment.h
  src/datadog/version.h
  src/datadog/w3c_propagation.h
)

add_dependencies(dd_trace_cpp-objects libcurl)

# Linking this library requires libcurl and threads.
find_package(Threads REQUIRED)
target_link_libraries(dd_trace_cpp-objects
  PUBLIC
    libcurl
    Threads::Threads
    ${COVERAGE_LIBRARIES}
    ${COREFOUNDATION_LIBRARY}
    ${SYSTEMCONFIGURATION_LIBRARY}
)

# Produce both shared and static versions of the library.
add_library(dd_trace_cpp-shared SHARED $<TARGET_OBJECTS:dd_trace_cpp-objects>)
set_target_properties(dd_trace_cpp-shared PROPERTIES OUTPUT_NAME "dd_trace_cpp")
add_dependencies(dd_trace_cpp-shared dd_trace_cpp-objects)
target_link_libraries(dd_trace_cpp-shared dd_trace_cpp-objects)

add_library(dd_trace_cpp-static STATIC $<TARGET_OBJECTS:dd_trace_cpp-objects>)
set_target_properties(dd_trace_cpp-static PROPERTIES OUTPUT_NAME "dd_trace_cpp")
add_dependencies(dd_trace_cpp-static dd_trace_cpp-objects)
target_link_libraries(dd_trace_cpp-static dd_trace_cpp-objects)

# When installing, install the static library, the shared library, and the
# public headers.
install(TARGETS dd_trace_cpp-static dd_trace_cpp-shared dd_trace_cpp-objects
  FILE_SET public_headers)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_BENCHMARK)
  add_subdirectory(benchmark)
endif()
