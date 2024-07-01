add_executable(fuzz-remote-config fuzz.cpp)

add_dependencies(fuzz-remote-config dd_trace_cpp-static)
target_link_libraries(fuzz-remote-config dd_trace_cpp-static)
