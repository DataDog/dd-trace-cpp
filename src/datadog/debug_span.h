#pragma once

// TODO

#include "optional.h"
#include "span.h"

namespace datadog {
namespace tracing {

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

  // If this object contains a span, then invoke the specified `visit` function
  // on the span. If this object does not contain a span, then do nothing.
  template <typename Visitor>
  void apply(Visitor&& visit) {
    if (child_) {
      visit(*child_);
    }
  }
};

}  // namespace tracing
}  // namespace datadog
