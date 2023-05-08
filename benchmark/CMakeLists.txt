# TODO: explain

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(dd_trace_cpp-benchmark
    benchmark.cpp
    hasher.cpp
)

# Google Benchmark is included as a git submodule.
# It depends on Google Test, which it will download if this option is set.
set(BENCHMARK_DOWNLOAD_DEPENDENCIES ON)
set(BENCHMARK_ENABLE_TESTING OFF)
add_subdirectory(google-benchmark)

target_link_libraries(dd_trace_cpp-benchmark benchmark::benchmark dd_trace_cpp)
