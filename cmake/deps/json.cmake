include(FetchContent)

find_package(nlohmann_json CONFIG QUIET)
if (NOT nlohmann_json_FOUND)
  set(JSON_BuildTests OFF)
  set(JSON_Install OFF)

  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
  )

  FetchContent_MakeAvailable(nlohmann_json)
endif()