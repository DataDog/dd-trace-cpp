#pragma once

// This component defines the internal span-link representation and its
// MessagePack serialization. Applications use `SpanContext` and
// `Span::add_link` instead.

#include <datadog/span_context.h>

namespace datadog::tracing {

class SpanLink {
 public:
  SpanContext context;
  SpanLinkAttributes attributes;

  SpanLink(SpanContext context, SpanLinkAttributes attributes = {})
      : context(std::move(context)), attributes(std::move(attributes)) {}
};

Expected<void> msgpack_encode(std::string& destination, const SpanLink& link);

}  // namespace datadog::tracing
