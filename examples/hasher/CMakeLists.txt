add_executable(hasher-example hasher.cpp)

add_dependencies(hasher-example dd_trace_cpp-static)

set_target_properties(hasher-example PROPERTIES OUTPUT_NAME example)

target_link_libraries(hasher-example dd_trace_cpp-static)
