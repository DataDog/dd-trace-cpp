add_executable(base64-fuzz main.cpp)

add_dependencies(base64-fuzz dd_trace_cpp-static)
target_link_libraries(base64-fuzz dd_trace_cpp-static)

add_target_to_group(base64-fuzz dd_trace_cpp-fuzzers)
