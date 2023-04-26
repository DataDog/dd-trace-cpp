cmake_minimum_required(VERSION 3.24)

project(dd-trace-cpp-benchmarks)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB BenchmarksSources *.cpp)
add_executable(gbench ${BenchmarksSources})

# Add Google Benchmark as a dependency
find_package(benchmark REQUIRED)
target_link_libraries(gbench benchmark::benchmark)
