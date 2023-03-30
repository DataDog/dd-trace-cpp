# Note: Make sure that this version is the same as that in
# "./CheckRequiredCMakeVersion.cmake".
cmake_minimum_required(VERSION 3.24)

project(dd-trace-cpp)

option(BUILD_COVERAGE "Build code with code coverage profiling instrumentation" OFF)
option(BUILD_EXAMPLE "Build the example program (example/)" OFF)
option(BUILD_TESTING "Build the unit tests (test/)" OFF)
option(SANITIZE "Build with address sanitizer and undefined behavior sanitizer" OFF)
option(FUZZ_W3C_PROPAGATION "Build a fuzzer for W3C propagation" OFF)

set(CMAKE_BUILD_TYPE "RelWithDebInfo")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# If we're building with clang, then use the libc++ version of the standard
# library instead of libstdc++. Better coverage of build configurations.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
endif()

include(ProcessorCount)
ProcessorCount(NUM_PROCESSORS)
set(MAKE_JOB_COUNT ${NUM_PROCESSORS} CACHE STRING "Number of jobs to use when building libcurl")

function(add_sanitizers)
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
  add_compile_options(-fsanitize=undefined)
  add_link_options(-fsanitize=undefined)
endfunction()

if(FUZZ_W3C_PROPAGATION)
  add_compile_options(-fsanitize=fuzzer)
  add_link_options(-fsanitize=fuzzer)
  add_sanitizers()
  add_subdirectory(fuzz/w3c-propagation)
endif()

if (SANITIZE)
  add_sanitizers()
endif()

if (BUILD_TESTING)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

include (ExternalProject)
ExternalProject_Add(curl
  URL "https://github.com/curl/curl/releases/download/curl-7_85_0/curl-7.85.0.tar.gz"
  URL_MD5 "4e9eb4f434e9be889e510f038754d3de"
  BUILD_IN_SOURCE 1
  DOWNLOAD_EXTRACT_TIMESTAMP 0
  SOURCE_DIR ${CMAKE_BINARY_DIR}/curl
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/curl/configure --prefix=${CMAKE_BINARY_DIR} --disable-ftp --disable-ldap --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-smtp --disable-gopher --without-ssl --disable-crypto-auth --without-axtls --without-zlib --disable-rtsp --enable-shared=no --enable-static=yes --with-pic --without-brotli
  BUILD_COMMAND make -j${MAKE_JOB_COUNT}
  INSTALL_COMMAND make install
)

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

add_library(dd_trace_cpp SHARED)
target_sources(dd_trace_cpp PRIVATE
    src/datadog/cerr_logger.cpp
    src/datadog/clock.cpp
    src/datadog/collector.cpp
    src/datadog/collector_response.cpp
    src/datadog/curl.cpp
    src/datadog/datadog_agent_config.cpp
    src/datadog/datadog_agent.cpp
    src/datadog/default_http_client_curl.cpp
#     src/datadog/default_http_client_null.cpp use libcurl
    src/datadog/dict_reader.cpp
    src/datadog/dict_writer.cpp
    src/datadog/environment.cpp
    src/datadog/error.cpp
    src/datadog/event_scheduler.cpp
    src/datadog/expected.cpp
    src/datadog/extracted_data.cpp
    src/datadog/glob.cpp
    src/datadog/hex.cpp
    src/datadog/http_client.cpp
    src/datadog/id_generator.cpp
    src/datadog/limiter.cpp
    src/datadog/logger.cpp
    src/datadog/msgpack.cpp
    src/datadog/null_collector.cpp
    src/datadog/optional.cpp
    src/datadog/parse_util.cpp
    src/datadog/platform_util.cpp
    src/datadog/propagation_style.cpp
    src/datadog/random.cpp
    src/datadog/rate.cpp
    src/datadog/sampling_decision.cpp
    src/datadog/sampling_mechanism.cpp
    src/datadog/sampling_priority.cpp
    src/datadog/sampling_util.cpp
    src/datadog/span_config.cpp
    src/datadog/span.cpp
    src/datadog/span_data.cpp
    src/datadog/span_defaults.cpp
    src/datadog/span_matcher.cpp
    src/datadog/span_sampler_config.cpp
    src/datadog/span_sampler.cpp
    src/datadog/string_view.cpp
    src/datadog/tag_propagation.cpp
    src/datadog/tags.cpp
    src/datadog/threaded_event_scheduler.cpp
    src/datadog/tracer_config.cpp
    src/datadog/tracer.cpp
    src/datadog/trace_id.cpp
    src/datadog/trace_sampler_config.cpp
    src/datadog/trace_sampler.cpp
    src/datadog/trace_segment.cpp
    src/datadog/version.cpp
    src/datadog/w3c_propagation.cpp
)

# This library's public headers are just its source headers.
target_sources(dd_trace_cpp PUBLIC
  FILE_SET public_headers
  TYPE HEADERS
  BASE_DIRS src/
  FILES
  src/datadog/cerr_logger.h
  src/datadog/clock.h
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
  src/datadog/glob.h
  src/datadog/hex.h
  src/datadog/http_client.h
  src/datadog/id_generator.h
  src/datadog/json_fwd.hpp
  src/datadog/json.hpp
  src/datadog/limiter.h
  src/datadog/logger.h
  src/datadog/msgpack.h
  src/datadog/null_collector.h
  src/datadog/optional.h
  src/datadog/parse_util.h
  src/datadog/platform_util.h
  src/datadog/propagation_style.h
  src/datadog/random.h
  src/datadog/rate.h
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
  src/datadog/string_view.h
  src/datadog/tag_propagation.h
  src/datadog/tags.h
  src/datadog/threaded_event_scheduler.h
  src/datadog/tracer_config.h
  src/datadog/tracer.h
  src/datadog/trace_id.h
  src/datadog/trace_sampler_config.h
  src/datadog/trace_sampler.h
  src/datadog/trace_segment.h
  src/datadog/version.h
  src/datadog/w3c_propagation.h
)

add_dependencies(dd_trace_cpp curl)

# Make the build libcurl visible to dd_trace_cpp, and its dependents.
target_include_directories(dd_trace_cpp PUBLIC ${CMAKE_BINARY_DIR}/include)

# Linking this library requires libcurl and threads.
find_package(Threads REQUIRED)
target_link_libraries(dd_trace_cpp PUBLIC ${CMAKE_BINARY_DIR}/lib/libcurl.a PUBLIC Threads::Threads ${COVERAGE_LIBRARIES})

# When installing, install the library and its public headers.

install(TARGETS dd_trace_cpp
  FILE_SET public_headers)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()

if(BUILD_EXAMPLE)
  add_subdirectory(example)
endif()
