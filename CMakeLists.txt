cmake_minimum_required(VERSION 3.13)

project(dd-trace-cpp)

option(BUILD_COVERAGE "Build code with code coverage profiling instrumentation" OFF)

set(CMAKE_BUILD_TYPE "RelWithDebInfo")

include (ExternalProject)
ExternalProject_Add(curl
  URL "https://github.com/curl/curl/releases/download/curl-7_85_0/curl-7.85.0.tar.gz"
  URL_MD5 "4e9eb4f434e9be889e510f038754d3de"
  BUILD_IN_SOURCE 1
  SOURCE_DIR ${CMAKE_BINARY_DIR}/curl
  CONFIGURE_COMMAND ${CMAKE_BINARY_DIR}/curl/configure --prefix=${CMAKE_BINARY_DIR} --disable-ftp --disable-ldap --disable-dict --disable-telnet --disable-tftp --disable-pop3 --disable-smtp --disable-gopher --without-ssl --disable-crypto-auth --without-axtls --without-zlib --disable-rtsp --enable-shared=no --enable-static=yes --with-pic --without-brotli
  BUILD_COMMAND make -j16
  INSTALL_COMMAND make install
)

set(CMAKE_CXX_STANDARD 17)

if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
    if(BUILD_COVERAGE)
      add_compile_options(-g -O0 -fprofile-arcs -ftest-coverage)
    endif()
endif()

# TODO: The sanitizers have been giving me "fatal errors" and other chicanery.
# add_compile_options(-fsanitize=address)
# add_link_options(-fsanitize=address)
# add_compile_options(-fsanitize=undefined)
# add_link_options(-fsanitize=undefined)
# add_compile_options(-fsanitize=thread)
# add_link_options(-fsanitize=thread)

if(BUILD_COVERAGE)
  set(COVERAGE_LIBRARIES gcov)
endif()

add_library(dd_trace_cpp
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
    src/datadog/glob.cpp
    src/datadog/http_client.cpp
    src/datadog/id_generator.cpp
    src/datadog/limiter.cpp
    src/datadog/logger.cpp
    src/datadog/msgpack.cpp
    src/datadog/net_util.cpp
    src/datadog/null_collector.cpp
    src/datadog/parse_util.cpp
    src/datadog/propagation_styles.cpp
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
    src/datadog/tag_propagation.cpp
    src/datadog/tags.cpp
    src/datadog/threaded_event_scheduler.cpp
    src/datadog/tracer_config.cpp
    src/datadog/tracer.cpp
    src/datadog/trace_sampler_config.cpp
    src/datadog/trace_sampler.cpp
    src/datadog/trace_segment.cpp
    src/datadog/version.cpp    
)

add_dependencies(dd_trace_cpp curl)

# Make the build libcurl visible to dd_trace_cpp and its dependents.
target_include_directories(dd_trace_cpp PUBLIC ${CMAKE_BINARY_DIR}/include)

# This library's public headers are just its source headers.
target_include_directories(dd_trace_cpp INTERFACE src/)

# Any dependent target that's linking this library will also need curl and threads.
find_package(Threads REQUIRED)
target_link_libraries(dd_trace_cpp INTERFACE ${CMAKE_BINARY_DIR}/lib/libcurl.a Threads::Threads)

if(BUILD_TESTING)
  add_subdirectory(test)
endif()

if(BUILD_EXAMPLE)
  add_subdirectory(example)
endif()
