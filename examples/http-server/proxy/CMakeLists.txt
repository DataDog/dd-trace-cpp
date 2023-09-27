add_executable(proxy-example proxy.cpp)

target_include_directories(proxy-example PRIVATE ../common)

target_link_libraries(proxy-example dd_trace_cpp-static)
