add_executable(gameday-example main.cpp)
set_target_properties(gameday-example PROPERTIES OUTPUT_NAME gameday-example)

target_link_libraries(gameday-example dd_trace_cpp-static)
