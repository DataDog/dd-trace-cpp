Scripts
=======
This directory contains scripts that are useful during development.

- [bazel-build](bazel-build) builds the library using [Bazel][1] via [bazelisk][2].
- [benchmark](benchmark) builds the library and its [benchmark](../benchmark),
  and runs the benchmark.
- [check](check) performs some of the checks that [continuous
  integration](../.circleci) performs. It's convenient to run this script before
  pushing changes.
- [check-format](check-format) verifies that the source code is formatted as
  [format](format) prefers.
- [check-version](check-version) accepts a version string as a command line
  argument (e.g. "v1.2.3") and checks whether the version within the source code
  matches. This is a good check to perform before publishing a source release.
- [cmake-build](cmake-build) builds the library using [CMake][3].
- [format](format) formats all of the C++ source code using
  [clang-format-14][4].
- [hasher-example](hasher-example) builds the library, including the [command
  line example](../examples/hasher) program, and then runs the example.
- [http-server-example](http-server-example) runs the docker compose based [HTTP
  server example](../examples/http-server).
- [install-cmake](install-cmake) installs a recent version of CMake if a more
  recent version is not installed already.
- [install-lcov](install-lcov) installs a version of GNU's code coverage HTML
  report generator that's recent enough to support dark mode.
- [release](release) checks the provided version string (e.g. "v1.2.3") and then
  publishes a "draft" "prerelease" GitHub source release, which you then can
  alter and document in the GitHub web UI.
- [test](test) builds the library, including the [unit tests](test), and then
  runs the unit tests.
- [with-toolchain](with-toolchain) is a command wrapper that sets the `CC` and
  `CXX` environment variables based on its first argument, which is either "gnu"
  (to use the gcc/g++ toolchain) or "llvm" (to use the clang/clang++ toolchain).
  For example: `with-toolchain llvm cmake -DDD_TRACE_BUILD_TESTING=1 ..`.

[1]: https://bazel.build/
[2]: https://github.com/bazelbuild/bazelisk
[3]: https://cmake.org/
[4]: https://releases.llvm.org/14.0.0/tools/clang/docs/ClangFormat.html
