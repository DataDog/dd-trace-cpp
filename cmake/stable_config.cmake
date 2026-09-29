find_program(DD_TRACE_CARGO cargo REQUIRED)

set(DD_TRACE_RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/stable-config-rust")
set(DD_TRACE_RUST_FILE_NAME
    "${CMAKE_STATIC_LIBRARY_PREFIX}dd_trace_cpp_stable_config_ffi${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(DD_TRACE_RUST_LIBRARY
    "${DD_TRACE_RUST_TARGET_DIR}/release/${DD_TRACE_RUST_FILE_NAME}")

add_custom_command(
  OUTPUT "${DD_TRACE_RUST_LIBRARY}"
  COMMAND ${CMAKE_COMMAND} -E env
          "CARGO_TARGET_DIR=${DD_TRACE_RUST_TARGET_DIR}"
          ${DD_TRACE_CARGO} build --release --locked
          --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/rust/stable-config-ffi/Cargo.toml"
  DEPENDS
    rust/stable-config-ffi/Cargo.toml
    rust/stable-config-ffi/Cargo.lock
    rust/stable-config-ffi/src/lib.rs
    vendor/libdatadog/libdd-library-config/src/lib.rs
    vendor/libdatadog/libdd-library-config/src/config_read.rs
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  VERBATIM
)

add_custom_target(dd-trace-cpp-stable-config-rust
  DEPENDS "${DD_TRACE_RUST_LIBRARY}")
add_library(dd-trace-cpp-stable-config-rust-lib STATIC IMPORTED GLOBAL)
set_target_properties(dd-trace-cpp-stable-config-rust-lib PROPERTIES
  IMPORTED_LOCATION "${DD_TRACE_RUST_LIBRARY}")
add_dependencies(dd-trace-cpp-stable-config-rust-lib
  dd-trace-cpp-stable-config-rust)

add_library(dd-trace-cpp-stable-config STATIC
  src/datadog/stable_config_loader.cpp)
add_library(dd-trace-cpp::stable-config ALIAS dd-trace-cpp-stable-config)
set_target_properties(dd-trace-cpp-stable-config PROPERTIES EXPORT_NAME stable-config)
target_compile_features(dd-trace-cpp-stable-config PUBLIC cxx_std_17)
target_include_directories(dd-trace-cpp-stable-config PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_sources(dd-trace-cpp-stable-config PUBLIC
  FILE_SET public_headers TYPE HEADERS BASE_DIRS include
  FILES include/datadog/stable_config_loader.h)
target_link_libraries(dd-trace-cpp-stable-config
  PUBLIC dd-trace-cpp-stable-config-rust-lib)
if (BUILD_SHARED_LIBS)
  target_link_libraries(dd-trace-cpp-stable-config PUBLIC dd-trace-cpp::shared)
elseif (BUILD_STATIC_LIBS)
  target_link_libraries(dd-trace-cpp-stable-config PUBLIC dd-trace-cpp::static)
else ()
  message(FATAL_ERROR "Stable config requires a dd-trace-cpp library")
endif ()

install(TARGETS dd-trace-cpp-stable-config
  EXPORT dd-trace-cpp-targets
  FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "${DD_TRACE_RUST_LIBRARY}"
  DESTINATION ${CMAKE_INSTALL_LIBDIR})
