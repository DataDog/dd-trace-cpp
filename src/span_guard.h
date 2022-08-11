#pragma once

#include "span.h"

namespace datadog {
namespace tracing {

class SpanGuard {
  Span span_;

 public:
  // TODO: Which of these deletions is needed?
  SpanGuard(const SpanGuard&) = delete;
  SpanGuard() = delete;

  explicit SpanGuard(Span span);
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
