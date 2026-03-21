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
