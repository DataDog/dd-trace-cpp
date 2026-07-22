# Fuzzers

Each subdirectory here contains the source of an executable that [fuzz
tests](https://en.wikipedia.org/wiki/Fuzzing) some part of the library using [LLVM's
libfuzzer](https://llvm.org/docs/LibFuzzer.html).

There is a toplevel CMake boolean option that adds all of the fuzzer executables to the build:
`BUILD_FUZZERS`.

When building the fuzzers, the toolchain must be clang-based. For example:

```shell
$ rm -rf .build # if toolchain needs clearing
$ bin/with-toolchain llvm bin/cmake-build -DDD_TRACE_BUILD_FUZZERS=1
$ .build/fuzz/w3c-propagation/w3c-propagation-fuzz

[... fuzzer output ...]
```

The fuzzer executables are named `.build/fuzz/*/*-fuzz` by convention.
