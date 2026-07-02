# Logical Component Relationships

- Vertices are components.
- Edges are ownership relationships between components. Each edge is labeled by the kind of pointer
  that is used to implement the relationship.
- Components with a padlock are protected by a mutex.

```mermaid
---
title: Components Relationships
---
graph LR;
  Tracer(Tracer) & TraceSegment("TraceSegment 🔒")-- shared -->Collector("Collector 🔒") & SpanSampler("SpanSampler 🔒") & TraceSampler("TraceSampler 🔒")
  TraceSegment-- "`**unique**`" -->SpanData(SpanData)
  Span(Span)-- shared -->TraceSegment
  Span-- "`**raw**`" -->SpanData
```

## Objects

- `Span` has a beginning, end, and tags. It is associated with a `TraceSegment`.
- `TraceSegment` is part of a trace. It makes sampling decisions, detects when it is finished, and
  sends itself to the `Collector`.
- `Collector` receives trace segments. It provides a callback to deliver sampler modifications, if
  applicable.
- `Tracer` is responsible for creating trace segments. It contains the instances of, and
  configuration for, the `Collector`, `TraceSampler`, and `SpanSampler`. A tracer is created from a
  `TracerConfig`.
- `TraceSampler` is used by trace segments to decide when to keep or drop themselves.
- `SpanSampler` is used by trace segments to decide which spans to keep when the segment is dropped.
- `TracerConfig` contains all of the information needed to configure the collector, trace sampler,
  and span sampler, as well as defaults for span properties.

## Usage

Intended usage is:

1. Create a `TracerConfig`.
2. Use the `TracerConfig` to create a `Tracer`.
3. Use the `Tracer` to create and/or extract local root `Span`s.
4. Use `Span` to create children and/or inject context.
5. Use a `Span`'s `TraceSegment` to perform trace-wide operations.
6. When all `Span`s in `TraceSegment` are finished, the segment is sent to the
   `Collector`.

Different instances of `Tracer` are independent of each other. If an application wishes to
reconfigure tracing at runtime, it can create another `Tracer` using the new configuration.
