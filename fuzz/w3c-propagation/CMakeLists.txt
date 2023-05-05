add_executable(fuzz fuzz.cpp)

add_dependencies(fuzz dd_trace_cpp-static)
target_link_libraries(fuzz dd_trace_cpp-static)
