# C Binding for dd-trace-cpp

A C binding interface on the C++ tracing library for
integration from C-based projects.

## Building

```sh
cmake -B build -DDD_TRACE_BUILD_C_BINDING=ON -DDD_TRACE_BUILD_TESTING=ON
cmake --build build -j
```

## Running Tests

```sh
./build/test/tests "[c_binding]"
```
