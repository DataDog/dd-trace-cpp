# Note: Make sure that this version is the same as that in
# "./CheckRequiredCMakeVersion.cmake".
cmake_minimum_required(VERSION 3.22)
cmake_policy(SET CMP0077 NEW)

project(dd-trace-cpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_STATIC_LIBS "Build static libraries" ON)

set(DD_TRACE_TRANSPORT "curl" CACHE STRING "HTTP transport that dd-trace-cpp uses to communicate with the Datadog Agent, can be either 'none' or 'curl'")

# Consumer of the library using FetchContent do not need
# to build unit tests, fuzzers and examples.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  option(DD_TRACE_BUILD_EXAMPLES "Build example programs" OFF)
  option(DD_TRACE_BUILD_TESTING "Build the unit tests (test/)" OFF)
  option(DD_TRACE_BUILD_FUZZERS "Build fuzzers" OFF)
  option(DD_TRACE_BUILD_BENCHMARK "Build benchmark binaries" OFF)
  option(DD_TRACE_ENABLE_COVERAGE "Build code with code coverage profiling instrumentation" OFF)
  option(DD_TRACE_ENABLE_SANITIZE "Build with address sanitizer and undefined behavior sanitizer" OFF)
endif()

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif ()

# Linking this library requires threads.
find_package(Threads REQUIRED)
include(cmake/deps/curl.cmake)
include(cmake/deps/json.cmake)
include(cmake/utils.cmake)

file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/src/datadog/version.cpp DD_TRACE_VERSION_CPP_CONTENTS REGEX "#define DD_TRACE_VERSION( |_NUM )")
string(REGEX MATCH "#define DD_TRACE_VERSION \"[^\"]*" DD_TRACE_VERSION ${DD_TRACE_VERSION_CPP_CONTENTS})
string(REGEX REPLACE "[^\"]+\"" "" DD_TRACE_VERSION ${DD_TRACE_VERSION})
message(STATUS "dd-trace-cpp version=[${DD_TRACE_VERSION}]")
unset(DD_TRACE_VERSION_CPP_CONTENTS)

message(STATUS "dd-trace-cpp transport=${DD_TRACE_TRANSPORT}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  message(STATUS "Compiler: MSVC ${CMAKE_CXX_COMPILER_VERSION}")
  include(cmake/compiler/msvc.cmake)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(STATUS "Compiler: clang ${CMAKE_CXX_COMPILER_VERSION}")
  include(cmake/compiler/clang.cmake)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
  message(STATUS "Compiler: clang-apple ${CMAKE_CXX_COMPILER_VERSION}")
  include(cmake/compiler/clang_apple.cmake)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  message(STATUS "Compiler: GCC ${CMAKE_CXX_COMPILER_VERSION}")
  include(cmake/compiler/gcc.cmake)
endif ()

if (DD_TRACE_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif ()

if (DD_TRACE_BUILD_TESTING)
  add_subdirectory(test)
endif()

if (DD_TRACE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif ()

if (DD_TRACE_BUILD_BENCHMARK)
  add_subdirectory(benchmark)
endif ()

add_library(dd_trace_cpp-objects OBJECT)
add_library(dd_trace::obj ALIAS dd_trace_cpp-objects)

file(GLOB_RECURSE public_header_files ${CMAKE_SOURCE_DIR}/include/*.h)

target_sources(dd_trace_cpp-objects
  PUBLIC
    ${public_header_files}
  PRIVATE
    src/datadog/base64.cpp
    src/datadog/cerr_logger.cpp
    src/datadog/clock.cpp
    src/datadog/config_manager.cpp
    src/datadog/collector_response.cpp
    src/datadog/datadog_agent_config.cpp
    src/datadog/datadog_agent.cpp
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
    src/datadog/parse_util.cpp
    src/datadog/platform_util.cpp
    src/datadog/propagation_style.cpp
    src/datadog/random.cpp
    src/datadog/rate.cpp
    src/datadog/remote_config/product.cpp
    src/datadog/remote_config/remote_config.cpp
    src/datadog/runtime_id.cpp
    src/datadog/span.cpp
    src/datadog/span_data.cpp
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

# Headers location are different depending of whether we are building 
# or installing the library.
target_include_directories(dd_trace_cpp-objects
  PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/datadog
)

target_link_libraries(dd_trace_cpp-objects
  PUBLIC
    Threads::Threads
  PRIVATE
    dd_trace::specs
)

# Produce both shared and static versions of the library.
if (BUILD_SHARED_LIBS)
  add_library(dd_trace_cpp-shared SHARED $<TARGET_OBJECTS:dd_trace_cpp-objects>)
  add_library(dd_trace::shared ALIAS dd_trace_cpp-shared)

  if (DD_TRACE_TRANSPORT STREQUAL "curl")
    add_dependencies(dd_trace_cpp-shared CURL::libcurl_shared)

    target_sources(dd_trace_cpp-shared
      PRIVATE
        src/datadog/curl.cpp
        src/datadog/default_http_client_curl.cpp
    )

    target_link_libraries(dd_trace_cpp-shared
      PRIVATE
        CURL::libcurl_shared
    )
  endif ()

  add_dependencies(dd_trace_cpp-shared dd_trace_cpp-objects CURL::libcurl_shared)

  target_link_libraries(dd_trace_cpp-shared
    PUBLIC
      dd_trace::obj
      CURL::libcurl_shared
    PRIVATE
      dd_trace::specs
  )

  install(TARGETS dd_trace_cpp-objects dd_trace_cpp-shared
    EXPORT dd_trace_cpp-export
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  # Install headers separately
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
  )
endif ()

if (BUILD_STATIC_LIBS)
  add_library(dd_trace_cpp-static STATIC $<TARGET_OBJECTS:dd_trace_cpp-objects>)
  add_library(dd_trace::static ALIAS dd_trace_cpp-static)

  if (DD_TRACE_TRANSPORT STREQUAL "curl")
    add_dependencies(dd_trace_cpp-static CURL::libcurl_static)

    target_sources(dd_trace_cpp-static
      PRIVATE
        src/datadog/curl.cpp
        src/datadog/default_http_client_curl.cpp
    )

    target_link_libraries(dd_trace_cpp-static 
      PRIVATE
        CURL::libcurl_static
    )
  endif ()

  add_dependencies(dd_trace_cpp-static dd_trace_cpp-objects)

  target_link_libraries(dd_trace_cpp-static 
    PUBLIC
      dd_trace::obj
    PRIVATE
      dd_trace::specs
  )

  install(TARGETS dd_trace_cpp-objects dd_trace_cpp-static
    EXPORT dd_trace_cpp-export
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )

  # Then, install the public headers separately
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
  )
endif ()
