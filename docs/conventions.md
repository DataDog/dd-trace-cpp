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

## Testing Frameworks

**Catch2** is used for unit tests.

**Google Benchmark** is used for performance benchmarks.

## To be Checked / Answered / Documented…

- Error handling options:
  - `std::variant<T, Error>`
  - homebrew a `std::expected<T>`
  - `throw Error(...)`
  - `RCode do_thing(T& output)`
  - `struct Result { Error error; T value; }`
- Is a `Span` RAII with respect to start/finish?
- When we begin calculating trace metrics within the tracer, we'll need to hit a `/stats` HTTP
  endpoint.
  - Does it live on the same thread as the `Collector`?
  - Do we invent a library-specific `HTTPClient` interface?
- Do we keep using cURL for the default `Collector`?
  - In-tree C++ library instead?
- Naming conventions:
  - `class TypeName;`
  - `.member_function();`
  - `free_function();`
  - `f(int func_arg);`
  - `int local_var;`
  - `int private_member_;`
  - `int public_member;`
  - `enum Color { red, green, blue };`
  - `which_one<TraceId, TraceID>`
- Can tracing be reconfigured at runtime?
- Can multiple tracers share a collector?
- Are rate limits per-tracer, per-process, or other?
- Which clang-format version and configuration do we use?
- Do we separate "public" and "private" APIs, or do we export all headers?
