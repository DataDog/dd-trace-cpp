add_executable(http-proxy-example proxy.cpp)

add_dependencies(http-proxy-example dd_trace_cpp-static)

target_include_directories(http-proxy-example PRIVATE ../common)

target_link_libraries(http-proxy-example dd_trace_cpp-static)

install(TARGETS http-proxy-example)
