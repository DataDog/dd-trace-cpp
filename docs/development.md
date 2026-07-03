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
