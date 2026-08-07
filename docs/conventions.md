# Datadog C++ Tracer Conventions

This document defines repository-wide conventions for `dd-trace-cpp`. Apply these conventions to all
new and modified code.

## C++ Version

The project targets **C++17** for broad compatibility.

All code must compile as C++17. Do not use features introduced in C++20 or later.

## Build Systems

- **CMake** is the primary build system supported, as documented in [the main Readme](../README.md).
Downstream consumers, such as the Datadog Nginx module, use CMake to embed the library (see
[nginx-datadog/CMakeLists.txt](https://github.com/DataDog/nginx-datadog/blob/c29a57f/CMakeLists.txt#L121)).
- **Bazel** is used internally for CI and by the Envoy integration (see
[envoy/source/extensions/tracers/datadog/BUILD](https://github.com/envoyproxy/envoy/blob/9d47ea91cc/source/extensions/tracers/datadog/BUILD#L52)).

## C Standard Libraries

The project supports:

- **glibc** (GNU C Library);
- [**musl**](https://en.wikipedia.org/wiki/Musl) (notably used by Alpine Linux).

## C++ Standard Library vs Abseil

Envoy uses [**Abseil**](https://abseil.io) instead of the C++ `std` types. It builds `dd-trace-cpp`
with the [-DDD_USE_ABSEIL_FOR_ENVOY
flag](https://github.com/envoyproxy/envoy/blob/9d47ea91cc/source/extensions/tracers/datadog/BUILD#L35-L38),
which is used in
[optional.h](https://github.com/DataDog/dd-trace-cpp/blob/v2.1.1/include/datadog/optional.h#L28-L49)
and
[string_view.h](https://github.com/DataDog/dd-trace-cpp/blob/v2.1.1/include/datadog/string_view.h#L30-L45).

## Header Files Organization

Public and private APIs are kept separate:

- Public headers live in `include/datadog` and are the only ones exported.
- Internal headers live in `src` and are not installed.

## Testing Frameworks

- Use **Catch2** for unit tests.
- Use **Google Benchmark** for performance benchmarks.

## Clean Code

- Use meaningful variable and function names.
- Do not use single-letter variable names.
- Avoid abbreviations, unless very common and unambiguous.
- Avoid obvious comments.
- Prefer clearer variable and function names over explanatory comments.
- Keep functions small and focused (<~ 20 lines when practical).
- When practical, place caller functions before callees, so the code can be read from top to bottom.

## C++ Code Style

- Use modern C++ idioms.
- Prefer explicit types. Use `auto` only for very long type names (>~ 50 characters).
- Never use C-style casts.
- Use raw pointers only when absolutely necessary.
- Use C++17 nested namespace syntax.
- Minimize the number of `#include` lines. Do not enforce the include-what-you-use rule.

## Naming Conventions

- class: `class TypeName;`
- class member function: `.member_function();`
- class public member: `int public_member;`
- class private member: `int private_member_;`
- free function: `free_function();`
- function argument: `function(int func_arg);`
- local variable: `int local_var;`
- enumeration: `enum class Color { RED, GREEN, BLUE };`
