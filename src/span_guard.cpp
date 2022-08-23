#include "span_guard.h"

#include <utility>

namespace datadog {
namespace tracing {

SpanGuard::SpanGuard(Span&& span) : span_(std::move(span)) {}

SpanGuard::~SpanGuard() { span_.finish(); }

Span& SpanGuard::value() { return span_; }
const Span& SpanGuard::value() const { return span_; }

Span& SpanGuard::operator*() { return span_; }
const Span& SpanGuard::operator*() const { return span_; }

Span* SpanGuard::operator->() { return &span_; }
const Span* SpanGuard::operator->() const { return &span_; }

}  // namespace tracing
}  // namespace datadog
