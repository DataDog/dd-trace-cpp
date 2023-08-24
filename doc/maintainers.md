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

`Tracer` has two methods:

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
TODO

### EventScheduler
TODO

### Configuration
TODO

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
