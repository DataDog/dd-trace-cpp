cmake_minimum_required(VERSION 3.13)

project(dd-trace-cpp)

set(CMAKE_BUILD_TYPE "RelWithDebInfo")
# https://www.mail-archive.com/search?l=gcc-bugs@gcc.gnu.org&q=subject:%22%5C%5BBug+tree%5C-optimization%5C%2F80635%5C%5D+%5C%5B8%5C%2F9%5C%2F10%5C%2F11+regression%5C%5D+std%5C%3A%5C%3Aoptional+and+bogus+%5C-Wmaybe%5C-uninitialized+warning%22&o=newest&f=1
add_compile_options(-Wno-error=maybe-uninitialized)

# set(CMAKE_BUILD_TYPE "Debug")

set(CMAKE_CXX_STANDARD 17)

if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
endif()

find_package(CURL)

add_library(dd_trace_cpp-object OBJECT
    src/cerr_logger.cpp
    src/clock.cpp
    src/collector.cpp
    src/collector_response.cpp
    src/datadog_agent_config.cpp
    src/datadog_agent.cpp
    src/default_http_client_curl.cpp
    # src/default_http_client_null.cpp we want to use libcurl
    src/dict_reader.cpp
    src/dict_writer.cpp
    src/error.cpp
    src/event_scheduler.cpp
    src/expected.cpp
    src/http_client.cpp
    src/id_generator.cpp
    src/logger.cpp
    src/msgpack.cpp
    src/propagation_styles.cpp
    src/rate.cpp
    src/sampling_decision.cpp
    src/sampling_mechanism.cpp
    src/sampling_priority.cpp
    src/span_config.cpp
    src/span.cpp
    src/span_data.cpp
    src/span_defaults.cpp
    src/span_sampler_config.cpp
    src/span_sampler.cpp
    src/tags.cpp
    src/threaded_event_scheduler.cpp
    src/tracer_config.cpp
    src/tracer.cpp
    src/trace_sampler_config.cpp
    src/trace_sampler.cpp
    src/trace_segment.cpp
)

target_link_libraries(dd_trace_cpp-object ${CURL_LIBRARIES})
set_property(TARGET dd_trace_cpp-object PROPERTY POSITION_INDEPENDENT_CODE ON)
