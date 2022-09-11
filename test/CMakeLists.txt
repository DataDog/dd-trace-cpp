
add_executable(tests main.cpp
    $<TARGET_OBJECTS:dd_trace_cpp-object>
    
    test_smoke.cpp
    test_tracer.cpp
)
add_dependencies(tests dd_trace_cpp-object)
target_link_libraries(tests ${CMAKE_BINARY_DIR}/lib/libcurl.a ${COVERAGE_LIBRARIES} Threads::Threads)
if(BUILD_COVERAGE)
    target_link_options(tests PRIVATE -fprofile-arcs -ftest-coverage)
endif()

target_include_directories(tests PRIVATE ../src/)
