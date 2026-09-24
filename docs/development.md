# Datadog C++ Tracer Development Processes

## Test

Pass `-DDD_TRACE_BUILD_TESTING=1` to `cmake` to include the unit tests in the build.

The resulting unit test executable is `test/tests` within the build directory.

```shell
cmake -B build -DDD_TRACE_BUILD_TESTING=1 .
cmake --build build -j
./build/test/tests
```

Alternatively, [bin/test](../bin/test) is provided for convenience.

## Code Style

C++ code is formatted using `clang-format-14`. Before submitting code changes, run the following
command:

```shell
bin/format
```

To check formatting without writing files:

```shell
bin/check-format
```

## Static Analysis

C++ is analyzed with **clang-tidy-14** (pinned; same major as `clang-format-14`)
using the shared `.clang-tidy` baseline. Warnings are errors.

Do not run clang-tidy on the host. Tidy must use the same compiler, flags, and
stdlib as the real build (`ci-clang` + libc++). `bin/check-tidy` re-execs in
`datadog/docker-library:dd-trace-cpp-ci-23768e9-*`, configures CMake there with
`DD_TRACE_ENABLE_CLANG_TIDY`, and builds `dd-trace-cpp-objects` so CMake
invokes `clang-tidy-14` with the exact compile line (first-party `src/` only):

```shell
bin/check-tidy
```

CI runs the same script in that image. A finding fails the pull request.

