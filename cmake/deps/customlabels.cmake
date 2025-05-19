include(FetchContent)

FetchContent_Declare(customlabels
  GIT_REPOSITORY https://github.com/DataDog/custom-labels.git
  GIT_TAG  elsa/add-process-storage
)

FetchContent_MakeAvailable(customlabels)

add_library(customlabels SHARED
  ${customlabels_SOURCE_DIR}/src/customlabels.c
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(TLS_DIALECT desc)
  elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
    set(TLS_DIALECT gnu2)
  else()
    message(FATAL_ERROR "Only aarch64 and x86-64 are supported (found: ${CMAKE_SYSTEM_PROCESSOR})")
  endif()

  include(CheckCompilerFlag)
  check_compiler_flag(CXX "-ftls-dialect=${TLS_DIALECT}" TLS_DIALECT_OK)
  if (TLS_DIALECT_OK)
      target_compile_options(customlabels PRIVATE
       -g
       -fPIC
       -ftls-model=global-dynamic
       -mtls-dialect=${TLS_DIALECT}
      )
  endif()
endif()

target_include_directories(customlabels
PUBLIC
  ${customlabels_SOURCE_DIR}/src
)
