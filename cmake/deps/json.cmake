include(FetchContent)

set(JSON_BuildTests OFF)
set(JSON_Install OFF)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)

FetchContent_MakeAvailable(json)
