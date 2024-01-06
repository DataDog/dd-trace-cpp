include(cmake/toolchain/clang.cmake)

if (APPLE)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
  find_library(SYSTEMCONFIGURATION_LIBRARY SystemConfiguration)
endif ()
