include(FetchContent)

set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(yaml-cpp
  URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
  URL_HASH SHA256=fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16
  EXCLUDE_FROM_ALL
  SYSTEM
)

# yaml-cpp 0.8.0 uses cmake_minimum_required(VERSION 3.4) which is rejected
# by CMake >= 4.0.  Allow it via CMAKE_POLICY_VERSION_MINIMUM.
set(_yaml_saved_policy_min "${CMAKE_POLICY_VERSION_MINIMUM}")
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(yaml-cpp)
set(CMAKE_POLICY_VERSION_MINIMUM "${_yaml_saved_policy_min}")

# Ensure yaml-cpp is compiled with the same sanitizer flags as the main
# project.  The sanitizers are set on dd-trace-cpp-specs as INTERFACE
# properties, which yaml-cpp doesn't link against.  Without this, MSVC
# ASAN annotation mismatches cause linker errors (LNK2038).
if (DD_TRACE_ENABLE_SANITIZE AND TARGET yaml-cpp)
  get_target_property(_sanitize_opts dd-trace-cpp-specs INTERFACE_COMPILE_OPTIONS)
  if (_sanitize_opts)
    target_compile_options(yaml-cpp PRIVATE ${_sanitize_opts})
  endif()
  get_target_property(_sanitize_link dd-trace-cpp-specs INTERFACE_LINK_LIBRARIES)
  if (_sanitize_link)
    target_link_libraries(yaml-cpp PRIVATE ${_sanitize_link})
  endif()
endif()
