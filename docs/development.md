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

Do not run clang-tidy on the host. `compile_commands.json` must be produced by
the same container that runs tidy (CMake, compiler, and sysroot). `bin/check-tidy`
re-execs in `datadog/docker-library:dd-trace-cpp-ci-23768e9-*`, configures CMake
there, and runs `clang-tidy-14`:

```shell
bin/check-tidy
```

CI runs the same script in that image. A finding fails the pull request.

