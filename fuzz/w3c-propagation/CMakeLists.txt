add_executable(fuzz fuzz.cpp)

target_link_libraries(fuzz dd_trace_cpp)
