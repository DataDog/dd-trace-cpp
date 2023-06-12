#pragma once

// TODO

#include "optional.h"
#include "span.h"

namespace datadog {
namespace tracing {

class DebugSpan {
  Optional<Span> child_;

 public:
  explicit DebugSpan(const Optional<Span>& parent) {
    if (parent) {
      child_.emplace(parent->create_child());
    }
  }

  template <typename Visitor>
  void apply(Visitor&& visit) {
    if (child_) {
      visit(*child_);
    }
  }
};

}  // namespace tracing
}  // namespace datadog
