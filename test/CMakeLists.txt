
add_executable(tests main.cpp
    $<TARGET_OBJECTS:dd_trace_cpp-object>
    
    smoke.cpp
)
add_dependencies(tests dd_trace_cpp-object)
target_link_libraries(tests ${CMAKE_BINARY_DIR}/lib/libcurl.a Threads::Threads)
target_include_directories(tests PRIVATE ../src/)
