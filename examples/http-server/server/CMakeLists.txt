add_executable(http-server-example server.cpp)

target_link_libraries(http-server-example dd_trace_cpp-static)
