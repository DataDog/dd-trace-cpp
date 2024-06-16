include(FetchContent)

# No need to build curl executable
SET(BUILD_CURL_EXE OFF)
SET(BUILD_SHARED_LIBS OFF)
SET(BUILD_STATIC_LIBS ON)
set(BUILD_LIBCURL_DOCS OFF)
set(BUILD_MISC_DOCS OFF)

# Disable all protocols except HTTP
SET(HTTP_ONLY ON)

# Disable curl features
SET(USE_ZLIB OFF)
SET(USE_LIBIDN2 OFF)
SET(CURL_ENABLE_SSL OFF)
SET(CURL_BROTLI OFF)
SET(CURL_ZSTD OFF)
SET(CURL_ZLIB OFF)
SET(CURL_USE_LIBSSH2 OFF)
SET(CURL_USE_LIBPSL OFF)
SET(CURL_DISABLE_HSTS ON)
set(CURL_CA_PATH "none")
set(CURL_CA_PATH_SET FALSE)
set(CURL_DISABLE_INSTALL ON)
set(CURL_DISABLE_ALTSVC ON)
set(CURL_DISABLE_SRP ON)

FetchContent_Declare(
  curl
  URL "https://github.com/curl/curl/releases/download/curl-8_8_0/curl-8.8.0.tar.gz"
  URL_MD5 "2300048f61e6196678281a8612a873ef"
)

FetchContent_MakeAvailable(curl)
