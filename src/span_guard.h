#pragma once

#include "span.h"

namespace datadog {
namespace tracing {

class SpanGuard {
  Span span_;

 public:
  explicit SpanGuard(Span&&);

  SpanGuard(const SpanGuard&) = delete;

  ~SpanGuard();

  Span& value();
  const Span& value() const;

  Span& operator*();
  const Span& operator*() const;

  Span* operator->();
  const Span* operator->() const;
};

}  // namespace tracing
}  // namespace datadog
