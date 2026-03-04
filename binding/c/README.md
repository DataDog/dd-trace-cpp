# C Binding for dd-trace-cpp

A C language binding (`ddog_trace_*`) wrapping the C++ tracing library for
integration from C-based projects.

## Building

```sh
cmake -B build -DBUILD_C_BINDING=ON -DDD_TRACE_BUILD_TESTING=ON .
cmake --build build -j
```

## Running Tests

```sh
./build/binding/c/test_c_binding
```
