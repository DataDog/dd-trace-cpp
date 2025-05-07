include(FetchContent)

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  set(TLS_DIALECT desc)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(TLS_DIALECT gnu2)
else()
  message(FATAL_ERROR "Only aarch64 and x86-64 are supported (found: ${CMAKE_SYSTEM_PROCESSOR})")
endif()

FetchContent_Declare(customlabels
  GIT_REPOSITORY https://github.com/DataDog/custom-labels.git
  GIT_TAG  elsa/add-process-storage
)

FetchContent_MakeAvailable(customlabels)

add_compile_options(
  -fPIC
  -ftls-model=global-dynamic
  -mtls-dialect=${TLS_DIALECT}
)

add_library(customlabels SHARED
  ${customlabels_SOURCE_DIR}/src/customlabels.c
)

target_include_directories(customlabels
PUBLIC
  ${customlabels_SOURCE_DIR}/src
)
