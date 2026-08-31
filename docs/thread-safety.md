# Datadog C++ Tracer Thread Safety

## Architecture Principles

- **Immutability**. The `Tracer` and the objects passed into a `TraceSegment` at construction are
set once and never mutated afterward.
- **Delegation**. All mutable shared state is in an explicit set of classes that each owns a
  `std::mutex`:
  - `CerrLogger`
  - `ConfigManager`
  - `Curl`
  - `DatadogAgent`
  - `SpanSampler::SynchronizedLimiter`
  - `Telemetry`
  - `ThreadedEventScheduler`
  - `TraceSampler`
  - `TraceSegment`
- `Span`/`SpanData`/`Tracer` carry no internal lock. `Span` is move-only and non-reassignable. The
  contract is: **one `Span`, one owner thread at a time**. Concurrency is meant to happen between
  sibling `Span`s of the same `TraceSegment`.
- **Use `std::mutex`, not `std::atomic`**:
  - Every synchronized class uses plain `std::mutex` + `lock_guard`/`unique_lock` (no usage of `std::atomic`), for simplicity.
  - No logging while holding a process-wide singleton's lock, because of potentially slow custom
    `Logger`.
- **The default pluggable interfaces add extra threads.** The default `HTTPClient`, which is `Curl`, and the default `EventScheduler`, which is `ThreadedEventScheduler`, add each one a dedicated thread.
- `Fork` (such as Nginx/Apache pre-fork workers). Because background threads are not automatically
  fork-safe, the embedder must construct the `Tracer` (and therefore any default
  `Curl`/`ThreadedEventScheduler`) strictly after `fork()`.
- **`Collector` sharing across multiple `Tracer`s**. This is the intended way to fan many
  threads/`Tracer`s into one flush pipeline. This is synchronized via `DatadogAgent`'s own mutex.
- **Three process-wide singletons, exceptions to the independence of `Tracer`s**:
  - `telemetry::instance()`: the first `Tracer` (of the process) configures the `Telemetry` used by
    all `Tracers`.
  - `root_session_id::get_or_init()` is meant to be caller-coordinated. Integrations (notably Nginx
    and Apache) should set this in the master process before workers fork so all `Tracer`s share the
    same root.
  - `OtelCtxRegistration` is a mutex-guarded singleton publishing the OpenTelemetry process context.
    Each `Tracer` registers/unregisters on construction/destruction. The first `Tracer`'s fields
    win. `service_instance_id` is published only while all live `Tracer`s agree on it.
- **Shutdown discipline**. The `DatadogAgent` destructor cancels scheduled tasks and waits for
  outstanding HTTP requests. `ThreadedEventScheduler`'s cancellation closure blocks until any
  in-flight callback finishes. `Curl`'s destructor joins its thread. This avoids callbacks firing
  into a destroyed object. A custom `HTTPClient`/`EventScheduler` must replicate this or accept
  trace loss on shutdown.
- **AppSec/WAF-style thread-pool offload**. This Nginx feature stresses the core library's
  unsynchronized `Span` (see details below).

## Take-aways for Library Users

- It is safe to share one `Tracer` across many threads. `create_span()`/`extract_span()` can be
  called concurrently. This is the primary supported model.
- It is safe to create/finish sibling `Span`s of the same `TraceSegment` concurrently across
  threads.
- It is unsafe to use a single `Span` object from two threads at once. Either transfer ownership
  completely (moves are supported) or build your own handoff protocol (see, for example,
  `nginx-datadog`'s WAF thread-pool integration: atomic release/acquire flag + swapping out the
  request's event handlers so the main thread can't touch it mid-flight).
- Never construct a `Tracer` before your process forks. Construct it after, in each child.
- Multiple `Tracer`s in one process share one telemetry pipeline.
- Multiple `Tracer`s in one process will end up with a `root_session_id` decided by whichever
  `Tracer` happened to construct first. To avoid this, you can you explicitly set
  `TracerConfig::root_session_id` (for example, Nginx and Apache both compute it once, pre-fork,
  then pass it explicitly).
- Multiple `Tracer`s in one process publish one shared OpenTelemetry process context. The first
  `Tracer`'s fields win. The shared `runtime_id` is published only while every `Tracer` agrees on
  it.
- If you supply your own `Collector`/`HTTPClient`/`EventScheduler`/`IDGenerator`/`Logger`, you must
  ensure it is safe to be called from multiple threads. The library will call it from whatever
  threads its other pluggable pieces run on.
