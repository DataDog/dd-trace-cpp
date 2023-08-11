#pragma once

// TODO

#include <cassert>

#include "optional.h"
#include "span.h"

namespace datadog {
namespace tracing {

// TODO: Move implementations to cpp file.

class DebugSpan {
  Optional<Span> child_;

 public:
  // If the specified `parent` contains a value, then create a child from it
  // and store the child in this object. If `parent` does not contain a value,
  // then do nothing.
  explicit DebugSpan(const Optional<Span>& parent) {
    if (parent) {
      child_.emplace(parent->create_child());
    }
  }

  explicit DebugSpan(const Span* parent) {
    if (parent) {
      child_.emplace(parent->create_child());
    }
  }

  // If this object contains a span, then invoke the specified `visit` function
  // on the span. If this object does not contain a span, then do nothing.
  template <typename Visitor>
  void apply(Visitor&& visit) {
    if (child_) {
      visit(*child_);
    }
  }

  const Span* get() const { return child_ ? &*child_ : nullptr; }
  Span* get() { return child_ ? &*child_ : nullptr; }
};

}  // namespace tracing
}  // namespace datadog