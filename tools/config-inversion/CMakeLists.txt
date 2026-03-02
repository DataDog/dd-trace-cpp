add_executable(config-inversion main.cpp)

target_compile_features(config-inversion
  PUBLIC
    cxx_std_17
)

target_include_directories(config-inversion
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(config-inversion
  PRIVATE
    nlohmann_json::nlohmann_json
    cxxopts::cxxopts
    dd-trace-cpp::specs
)
