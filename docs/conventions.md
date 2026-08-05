# Datadog C++ Tracer Conventions and Rationale

## C++ Version

We use **C++17** to ensure maximum compatibility.

## Build Systems

**CMake** is the primary build system supported, as documented in [the main Readme](../README.md). It is
how downstreams consumers, such as the Datadog Nginx module, embed the library (see
[nginx-datadog/CMakeLists.txt](https://github.com/DataDog/nginx-datadog/blob/c29a57f/CMakeLists.txt#L121)).

**Bazel** is used internally for CI and by the Envoy integration (see
[envoy/source/extensions/tracers/datadog/BUILD](https://github.com/envoyproxy/envoy/blob/9d47ea91cc/source/extensions/tracers/datadog/BUILD#L52)).

## C Standard Libaries

We support **glibc** (GUN C Library) and [**musl**](https://en.wikipedia.org/wiki/Musl) (notably used by Alpine Linux).

## C++ Standard Library vs Abseil

Envoy uses [**Abseil**](https://abseil.io) instead of the C++ `std` types. It builds `dd-trace-cpp`
with the [-DDD_USE_ABSEIL_FOR_ENVOY
flag](https://github.com/envoyproxy/envoy/blob/9d47ea91cc/source/extensions/tracers/datadog/BUILD#L35-L38),
which is used in
[optional.h](https://github.com/DataDog/dd-trace-cpp/blob/v2.1.1/include/datadog/optional.h#L28-L49)
and
[string_view.h](https://github.com/DataDog/dd-trace-cpp/blob/v2.1.1/include/datadog/string_view.h#L30-L45).

## Header Files Organization

We separate public and private APIs. Public headers live in `include/datadog` and are the only ones exported. Internal headers live in `src` and are not installed.

## Testing Frameworks

**Catch2** is used for unit tests.

**Google Benchmark** is used for performance benchmarks.

## Clean Code

- Use meaningfull variable and function names.
- Do not use single-letter variable names.
- Avoid abbreviations, unless very common and unambiguous.
- Avoid ovbious comments.
- Prefer clearer variable and function names over explanatory comments.
- Keep functions small and focused (<~ 20 lines when practical).
- When practical, place caller functions before callees, so the code can be read from top to bottom.

## C++ Code Style

- Use modern C++ (C++17).
- For readability, avoid `auto` unless for very long type names (>~ 50 characters).
- Never use C-style casts.
- Use C++17 nested namespace syntax.
- Minimize the number of `#include lines`. Do not enforce the include-what-you-use rule.

## Naming Conventions

- `class TypeName;`
- `.member_function();`
- `free_function();`
- `f(int func_arg);`
- `int local_var;`
- `int private_member_;`
- `int public_member;`
- `enum Color { red, green, blue };`
- `which_one<TraceId, TraceID>`
