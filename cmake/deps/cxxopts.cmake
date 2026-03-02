include(FetchContent)

FetchContent_Declare(cxxopts
  URL https://github.com/jarro2783/cxxopts/archive/refs/tags/v3.3.1.tar.gz
  URL_HASH SHA256=3bfc70542c521d4b55a46429d808178916a579b28d048bd8c727ee76c39e2072
  FIND_PACKAGE_ARGS NAMES cxxopts
  EXCLUDE_FROM_ALL
  SYSTEM
)

FetchContent_MakeAvailable(cxxopts)
