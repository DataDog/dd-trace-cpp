Fuzzers
=======
Each subdirectory here contains the source of an executable that [fuzz tests][1]
some part of the library using [LLVM's libfuzzer][2].

There is a toplevel CMake boolean option associated with each fuzzer. The naming
convention is `FUZZ_<SUBDIRECTORY_WITH_UNDERSCORES>`, e.g.
`FUZZ_W3C_PROPAGATION` for the fuzzer defined in
[fuzz/w3c-propagation/](./w3c-propagation/). The resulting binary is called
`fuzz` by convention.

When building a fuzzer, the toolchain must be clang-based.  For example, this
is how to build the fuzzer in [fuzz/w3c-propagation](./w3c-propagation/) from
the root of the repository:
```console
$ rm -rf .build && mkdir .build # if toolchain or test setup need clearing
$ cd .build
$ CC=clang CXX=clang++ cmake .. -DFUZZ_W3C_PROPAGATION=ON
$ make -j $(nproc)
$ fuzz/w3c-propagation/fuzz

[... fuzzer output ...]
```

[1]: https://en.wikipedia.org/wiki/Fuzzing
[2]: https://llvm.org/docs/LibFuzzer.html
