include(FetchContent)

# Header-only; used only by the test suite to decode msgpack payloads written
# by the library, so we don't need Boost, its own tests, examples, or docs.
set(MSGPACK_USE_BOOST OFF CACHE BOOL "" FORCE)
set(MSGPACK_CXX17 ON CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MSGPACK_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(msgpack-cxx
  URL https://github.com/msgpack/msgpack-c/archive/refs/tags/cpp-8.0.0.tar.gz
  URL_HASH SHA256=f634fb7052da4478096f2a02dfb6d91174e5836b317afb006375249ccb086aa8
  FIND_PACKAGE_ARGS NAMES msgpack-cxx
  EXCLUDE_FROM_ALL
  SYSTEM
)

FetchContent_MakeAvailable(msgpack-cxx)
