include(FetchContent)

FetchContent_Declare(customlabels
  GIT_REPOSITORY https://github.com/DataDog/custom-labels.git
  GIT_TAG  elsa/add-process-storage
)

FetchContent_MakeAvailable(customlabels)

add_library(customlabels SHARED
  ${customlabels_SOURCE_DIR}/src/customlabels.c
)

target_include_directories(customlabels
PUBLIC
  ${customlabels_SOURCE_DIR}/src
)
