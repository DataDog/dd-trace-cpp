cmake_minimum_required(VERSION 3.13)

project(dd-trace-cpp)

set(CMAKE_BUILD_TYPE "RelWithDebInfo")
# set(CMAKE_BUILD_TYPE "Debug")

set(CMAKE_CXX_STANDARD 17)

if (MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -pedantic -Werror)
endif()

find_package(CURL)

add_library(dd_trace_cpp-object OBJECT
    src/datadog/cerr_logger.cpp
    src/datadog/clock.cpp
    src/datadog/collector.cpp
    src/datadog/collector_response.cpp
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

target_link_libraries(dd_trace_cpp-object ${CURL_LIBRARIES})
set_property(TARGET dd_trace_cpp-object PROPERTY POSITION_INDEPENDENT_CODE ON)
