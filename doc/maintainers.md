Care and Feeding of Your New Tracing Library
============================================
Congratulations! You are now the proud owner of a distributed tracing library.

The primary purpose of this guide is to describe salient features of the
library's design. dd-trace-cpp differs considerably from its [older sibling][1]
and [peers][2].

This guide will also cover operations performed by maintainers of the library,
such as scooping the box, applying flea medication, and regular trips to the
vet.

Design
------

### Span
[class Span][3] is the component with which users will interact the most.
Each span:

- has an "ID,"
- is associated with a "trace ID,"
- is associated with a "service," which has a "service type," a "version," and
  an "environment,"
- has a "name" (sometimes called the "operation name"),
- has a "resource name," which is a description of the thing that the span is
  about,
- contains information about whether an error occurred during the represented
  operation, including an error message, error type, and stack trace,
- includes an arbitrary name/value mapping of strings, called "tags,"
- has a start time indicating when the represented operation began,
- has a duration indicating how long the represented operation took to finish.

Aside from setting and retrieving its attributes, `Span` also has the following
operations:

- `parent.create_child(...)` returns a new `Span` that is a child of `parent`.
- `span.inject(writer)` writes trace propagation information to a
  [DictWriter][11], which is an interface for setting a name/value mapping, e.g.
  in HTTP request headers.

A `Span` does not own its data. `class Span` contains a raw pointer to a [class
SpanData][4], which contains the actual attributes of the span. The `SpanData`
is owned by a `TraceSegment`, which is described in the next section. The
`Span` holds a `shared_ptr` to its `TraceSegment`.

By default, a span's start time is when it is created, and its end time (from
which its duration is calculated) is when it is destroyed. However, a span's
start time can be specified when it is created, via `SpanConfig::start` (see
[span_config.h][5]), and a span's end time can be overridden via
`Span::set_end_time`.

When a span is destroyed, it is considered "finished" and notifies its
`TraceSegment`. There is no way to "finish" a span without destroying it. You
can override its end time throughout the lifetime of the `Span` object, but a
`TraceSegment` does not consider the span finished until the `Span` object is
destroyed. This allows us to avoid "finished" `Span` states.

Along similar lines, `class Span` is move-only. Its copy constructor is deleted.
Functions that produce spans return them by value, but only one copy of a span
can exist at a time. In fact, `class Span` is even more strict than move-only:
its assignment operator is deleted, including the move-assignment operator. To
see why, consider the following (disallowed) example:
```c++
Span span = tracer.create_span();
// ...
// Let's reuse the variable `span`.
span = tracer.create_span();
```
Move assignment begins with two objects and ends up with one object (and one
empty shell of an object).

Since destroying a `Span` has the side effect of finishing it, one sensible
definition of `Span::operator=(Span&& other)` would be equivalent to:
```c++
this->~Span();
new (this) Span(std::move(other));
return *this;
```
This would have the potentially surprising feature of _finishing_ the first span
when you wish to replace it with another, i.e. there would always be two spans.

This could be avoided if we could guarantee that the two `Span`s belong to the
same `TraceSegment`. Then move-assigning a `Span` could be defined as
move-assigning its `SpanData` and somehow annotating the moved-from `SpanData`
as being invalid.  However, if the two `Span`s belong to different
`TraceSegment`s, then it could be that the moved-to `Span`'s `TraceSegment`
consists of only that one `Span`. Now we have to account for empty
`TraceSegment` states. This could all be dealt with, but no matter what we
decide, it would always be the case that `Span::operator=(Span&&)` has the
effect of making the original span (`this`) either finish implicitly or
_disappear entirely_, which is at odds with its otherwise [RAII][6] nature.

To avoid these issues, assignment to `Span` objects is disallowed.

Another opinionated property of `Span` is that it is not an interface, nor does
it implement an interface. Usually it is considered polite for a C++ library to
deal in handles (`unique_ptr` or `shared_ptr`) to interfaces, i.e. classes that
contain pure virtual functions. This way, a client of the library can substitute
an alternative implementation to the interface(s) for testing or for when the
behavior of the library is not desired.

At the risk of being impolite, dd-trace-cpp takes a different approach. `Span`
is a concrete type whose behavior cannot be substituted. Instead, there are
other places in the library where dependency injection can be used to restrict
or alter the behavior of the library. The trade-off is that `Span` and related
components must always "go through the motions" of their definitions and cannot
be completely customized, but in exchange the indirection, pointer semantics,
and null states that accompany handle-to-interface are avoided.

### Trace Segment
A "trace" is the entire tree of spans having the same trace ID.

Within one process/worker/service, though, typically there is not an entire
trace but only part of the trace. Let's call the process/worker/service a
"tracer."

One portion of a trace that's passing through the tracer is called a "trace
segment." A trace segment begins either at the trace's root span or at a span
extracted from trace context, e.g. a span created from the `X-Datadog-Trace-Id`
and `X-Datadog-Parent-Id` HTTP request headers. The trace segment includes all
local descendants of that span, and has as its "boundary" any descendant spans
without children or descendant spans that were used to inject trace context
out-of-tracer, e.g. in outgoing HTTP request headers.

There might be more than one trace segment for the _same trace_ within a tracer
at the same time. Consider the diagram below.

<img src="segments.jpg" width="400" alt="flame graph"/>

If our tracer is "service X," then this trace passes through the tracer twice.
We would have two concurrent trace segments for the same trace.

`class TraceSegment` is defined in [trace_segment.h][7]. `TraceSegment` objects
are managed internally by the library. That is to say, a user never creates a
`TraceSegment`.

The library creates a `TraceSegment` whenever a new trace is created or when
trace context is extracted. This is the job of `class Tracer`, described in the
next section.

Primarily, `TraceSegment` is a bag of spans. It contains a
`vector<unique_ptr<SpanData>>`. `Span` objects then refer to the `SpanData`
objects via raw pointers. Now that I think about it, `deque<SpanData>` would
work just as well.

When one of a trace segment's spans creates a child, the child is registered
with the trace segment. When a span is finished, the trace segment is notified.
The trace segment keeps track of how many spans it contains (the size of its
`vector`) and how many spans are finished. When the two numbers are equal, the
trace segment is finished.

When a trace segment is finished, it performs some finalization logic in order
to prepare its spans for submission to the `Collector`. Then it moves its spans
into the collector via `Collector::send`, and a short time later the trace
segment is destroyed. See `TraceSegment::span_finished` in
[trace_segment.cpp][8]. `Collector` is described in a subsequent section.

A `TraceSegment` contains `shared_ptr`s to everything that it needs in order to
do its job. Those objects are created by `class Tracer` when the tracer is
configured, and then shared with `TraceSegment` when the `TraceSegment` is
created.

### Tracer
`class Tracer` is what users configure, and it is how `Span`s are extracted from
trace context or created as a trace's root. See [tracer.h][9].

`Tracer` has two member functions:

- `create_span(...)`
- `extract_span(...)`

and another that combines them:

- `extract_or_create_span(...)`.

All of these result in the creation of a new `TraceSegment` (or otherwise return
an error). The `Tracer`'s data members, which were initialized based on the
tracer's configuration, are copied into the `TraceSegment` so that the
`TraceSegment` can operate independently.

Note how `create_span` never fails. This is a nice property. `extract_span`
_can_ fail.

The bulk of `Tracer`'s implementation is `extract_span`. The other substantial
work is configuration, which is handled by `finalize_config(const
TracerConfig&)`, declared in [tracer_config.h][10]. Configuration will be
described in more depth in a subsequent section.

### Collector
`class Collector` is an interface for sending a `TraceSegment`'s spans somewhere
once they're all done. It's defined in [collector.h][12].

It's just one function: `send`. More of a callback than an interface.

A `Collector` is either created by `Tracer` or injected into its configuration.
The `Collector` instance is then shared with all `TraceSegment`s created by the
`Tracer`. The only thing that a `TraceSegment` does with the `Collector` is call
`send` once the segment is finished.

The default implementation is `DatadogAgent`, which is described in the next
section.

### DatadogAgent
`class DatadogAgent` is the default implementation of `Collector`. It's defined
in [datadog_agent.h][13].

`DatadogAgent` sends trace segments to the [Datadog Agent][14] in batches that
are flushed periodically. In order to do this, `DatadogAgent` needs a means to
make HTTP requests and a means to set a timer for the flush operation. So,
there are two interfaces: [HTTPClient][15] and [EventScheduler][16].

The `HTTPClient` and `EventScheduler` can be injected as part of
`DatadogAgent`'s [configuration][17], which is usually specified via the `agent`
member of `Tracer`'s [configuration][10]. If they're not specified, then default
implementations are used:

- [class Curl : public HTTPClient][18], which uses libcurl's [multi
  interface][20] together with a dedicated thread as an event loop.
- [class ThreadedEventScheduler : public EventScheduler][19], which uses a
  dedicated thread for executing scheduled events at the correct time.

`DatadogAgent::flush` is periodically called by the event scheduler. `flush`
uses the HTTP client to send a POST request to the Datadog Agent's
[/v0.4/traces][21] endpoint. It's all callback-based.

### HTTPClient
`class HTTPClient` is an interface for sending HTTP requests. It's defined in
[http_client.h][15].

The only kind of HTTP request that the library needs to make, currently, is a
POST to the Datadog Agent's traces endpoint. `HTTPClient` has one member
function for each HTTP method needed — so, currently just the one:
```c++
virtual Expected<void> post(const URL& url, HeadersSetter set_headers,
                            std::string body, ResponseHandler on_response,
                            ErrorHandler on_error) = 0;
```
It's callback-based. `post` returns almost immediately. It invokes `set_headers`
before returning, in order to get the HTTP request headers. The request `body`
is moved elsewhere for later processing. One of `on_response` or `on_error` will
eventually be called, depending on whether a response was received or if an
error occurred before a response was received. If something goes wrong setting
up the request, then `post` returns an error. If `post` returns an error, then
neither of `on_response` nor `on_error` will be called.

`HTTPClient` also has another member function:
```c++
virtual void drain(std::chrono::steady_clock::time_point deadline) = 0;
```
`drain` waits for any in-flight requests to finish, blocking up until no later
than `deadline`. It's used to ensure "clean shutdown." Without it, on average
the last one second of traces would be lost on shutdown. Implementations of
`HTTPClient` that don't have a dedicated thread need not support `drain`; in
those cases, `drain` returns immediately.

The default implementation of `HTTPClient` is [class Curl : public
HTTPClient][18], which uses libcurl's [multi interface][20] together with a
dedicated thread as an event loop.

`class Curl` is also used within NGINX in Datadog's NGINX module,
[nginx-datadog][22]. This is explicitly [discouraged][23] in NGINX's developer
documentation, but libcurl-with-a-thread is widely used within NGINX modules
regardless. One improvement that I am exploring is to use libcurl's
"[multi_socket][24]" mode, which allows libcurl to utilize someone else's event
loop, obviating the need for another thread. libcurl can then be made to use
NGINX's event loop, as is done in [an example library][25].

For now, though, nginx-datadog uses the threaded `class Curl`.

[Envoy's Datadog tracing integration][26] uses a different implementation,
[class AgentHTTPClient : public HTTPClient, ...][27], which uses Envoy's
built-in HTTP facilities. libcurl is not involved at all.

### EventScheduler
As of this writing, `class DatadogAgent` flushes batches of finished trace
segments to the Datadog Agent once every two second [by default][28].

It does this by scheduling a recurring event with an `EventScheduler`, which is
an interface defined in [event_scheduler.h][16].

`EventScheduler` has one member function:
```c++
virtual Cancel schedule_recurring_event(
    std::chrono::steady_clock::duration interval,
    std::function<void()> callback) = 0;
```
Every `interval`, the scheduler will invoke `callback`, starting an initial
`interval` after `schedule_recurring_event` is called. The caller can invoke the
returned `Cancel` to prevent subsequent invocations of `callback`.

The default implementation of `EventScheduler` is [class ThreadedEventScheduler
: public EventScheduler][19], which uses a dedicated thread for executing
scheduled events at the correct time. It was a fun piece of code to write.

Datadog's NGINX module, [nginx-datadog][22] uses a different implementation,
[class NgxEventScheduler : public EventScheduler][29], which uses NGINX's own
event loop instead of a dedicated thread.

[Envoy's Datadog tracing integration][26] also uses a different implementation,
[class EventScheduler : public EventScheduler][30], which uses Envoy's built-in
event dispatch facilities.

### Configuration
There's a good [blog post][32] by [Alexis King][31] where she makes the case for
encoding configuration validation into the type system. Forbid invalid states by
making configurable components accept a different type than that which is used
to specify configuration.

This is not a new idea. It's been used, for example, to "taint" strings that
originate as program inputs. Then you can't accidentally pass user-influenced
inputs to, say, `std::system`, because `std::system` takes a `const char*`, not
a `class UserTaintedString`. There's still ample opportunity to cast away the
taint and sneak it into some string building operation, but at least `class
UserTaintedString` gives hope that a static analysis tool could be used to fill
in some gaps in human code review.

This library adopts that approach for configuration. The configuration of `class
Tracer` is `class TracerConfig`, but in order to construct a `Tracer` you must
first convert the `TracerConfig` into a `FinalizedTracerConfig` by calling
`finalize_config`. If there is anything wrong with the `TracerConfig` or with
environment variables that would override it, `finalize_config` will return an
`Error` instead of a `FinalizedTracerConfig`. In that case, you can't create a
`Tracer` at all.

This technique applies to multiple components:

| Component | Unvalidated | Validated | Parser |
| --------------- | ----------- | --------- | ----------------- |
| `Tracer` | `TracerConfig` | `FinalizedTracerConfig` | `finalize_config` in [tracer_config.h][10] |
| `DatadogAgent` | `DatadogAgentConfig` | `FinalizedDatadogAgentConfig` |  `finalize_config` in [datadog_agent_config.h][17] |
| `TraceSampler` | `TraceSamplerConfig` | `FinalizedTraceSamplerConfig` |  `finalize_config` in [trace_sampler_config.h][33] |
| `SpanSampler` | `SpanSamplerConfig` | `FinalizedSpanSamplerConfig` |  `finalize_config` in [span_sampler_config.h][34] |
| multiple | `double` | `Rate` | `Rate::from` in [rate.h][35] |

An alternative approach, that Caleb espouses, is to accept invalid configuration
quietly. When invalid configuration is detected, the library could substitute a
reasonable default and then send notice of the configuration issue to Datadog,
e.g. as a hidden span tag. That information would then be available to Support
should the customer raise an issue due to a difference in behavior between what
they see and what they think they configured.

This library uses the stricter approach. The downside is that a user of the
library has to decide what to do when even the slightest part of the
configuration or environment is deemed invalid.

One other convention of the library is that `FinalizedFooConfig` (for some
`Foo`) is never a data member of the configured component class. That is,
`FinalizedTracerConfig` is not stored in `Tracer`. Instead, a constructor might
individually copy the finalized config's data members. This is to prevent
eventual intermixing between the "configuration representation" and the "runtime
representation." In part, `finalize_config` already mitigates the problem.
Abstaining from storing the finalized config as a data member is a step further.

### Error Handling
TODO

### Logging
TODO

Operations
----------
TODO

### Testing
TODO

### Code Coverage
TODO

### Benchmarks
TODO

### Continuous Integration
TODO

### Releases
TODO

### Support
TODO

[1]: https://github.com/DataDog/dd-opentracing-cpp
[2]: https://github.com/open-telemetry/opentelemetry-cpp
[3]: ../src/datadog/span.h
[4]: ../src/datadog/span_data.h
[5]: ../src/datadog/span_config.h
[6]: https://en.cppreference.com/w/cpp/language/raii
[7]: ../src/datadog/trace_segment.h
[8]: ../src/datadog/trace_segment.cpp
[9]: ../src/datadog/tracer.h
[10]: ../src/datadog/tracer_config.h
[11]: ../src/datadog/dict_writer.h
[12]: ../src/datadog/collector.h
[13]: ../src/datadog/datadog_agent.h
[14]: https://docs.datadoghq.com/agent/
[15]: ../src/datadog/http_client.h
[16]: ../src/datadog/event_scheduler.h
[17]: ../src/datadog/datadog_agent_config.h
[18]: ../src/datadog/curl.h
[19]: ../src/datadog/threaded_event_scheduler.h
[20]: https://curl.se/libcurl/c/libcurl-multi.html
[21]: https://github.com/DataDog/datadog-agent/blob/9d57c10a9eeb3916e661d35dbd23c6e36395a99d/pkg/trace/api/version.go#L22
[22]: https://github.com/DataDog/nginx-datadog
[23]: https://nginx.org/en/docs/dev/development_guide.html#http_requests_to_ext
[24]: https://curl.se/libcurl/c/curl_multi_socket_action.html
[25]: https://github.com/dgoffredo/nginx-curl
[26]: https://github.com/envoyproxy/envoy/tree/main/source/extensions/tracers/datadog#datadog-tracer
[27]: https://github.com/envoyproxy/envoy/blob/main/source/extensions/tracers/datadog/agent_http_client.h
[28]: https://github.com/DataDog/dd-trace-cpp/blob/ca155b3da65c2dc235cf64a28f8e0d8fdab3700c/src/datadog/datadog_agent_config.h#L50-L51
[29]: https://github.com/DataDog/nginx-datadog/blob/master/src/ngx_event_scheduler.h
[30]: https://github.com/envoyproxy/envoy/blob/main/source/extensions/tracers/datadog/event_scheduler.h
[31]: https://lexi-lambda.github.io/about.html
[32]: https://lexi-lambda.github.io/blog/2019/11/05/parse-don-t-validate/
[33]: ../src/datadog/trace_sampler_config.h
[34]: ../src/datadog/span_sampler_config.h
[35]: ../src/datadog/rate.h
