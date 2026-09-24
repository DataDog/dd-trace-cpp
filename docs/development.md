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

C++ is analyzed with clang-tidy using the shared `.clang-tidy` baseline (kept in
sync with `httpd-datadog` and `nginx-datadog`). Warnings are errors.

Configure CMake so a compilation database exists, then run tidy:

```shell
cmake . -B .build --preset dev
bin/check-tidy
```

`BUILD_DIR` defaults to `.build`. CI runs the same check in the Development
workflow after `ci-clang` configure; a finding fails the pull request.

