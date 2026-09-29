include(FetchContent)

set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL ON CACHE BOOL "" FORCE)
set(YAML_CPP_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
set(YAML_ENABLE_PIC ON CACHE BOOL "" FORCE)
if (DD_TRACE_STATIC_CRT)
  set(YAML_MSVC_SHARED_RT OFF CACHE BOOL "" FORCE)
endif ()

FetchContent_Declare(yaml-cpp
  URL https://github.com/jbeder/yaml-cpp/releases/download/yaml-cpp-0.9.0/yaml-cpp-yaml-cpp-0.9.0.tar.gz
  URL_HASH SHA256=298593d9c440fd9034b8b193d96318b76d49bc97c6ceadb7b0836edf0b6d7539
  SYSTEM
)
FetchContent_MakeAvailable(yaml-cpp)
