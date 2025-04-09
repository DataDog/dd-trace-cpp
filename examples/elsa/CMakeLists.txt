add_executable(telemetry-example main.cpp)
set_target_properties(telemetry-example PROPERTIES OUTPUT_NAME example)

target_link_libraries(telemetry-example dd_trace_cpp-static)
