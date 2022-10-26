#pragma once

// This component provides an interface, `Collector`, to which spans of
// completed trace segments can be sent.
//
// `DatadogAgent`, defined in `datadog_agent.h`, implements `Collector` by
// serializing the spans and sending them to a Datadog Agent.
//
// As a result of `send`ing spans to a `Collector`, the `TraceSampler` might be
// adjusted to increase or decrease the rate at which traces are kept.  See the
// `response_handler` parameter to `Collector::send`.

#include <memory>
#include <optional>
#include <vector>

#include "expected.h"

namespace datadog {
namespace tracing {

struct SpanData;
class TraceSampler;

class Collector {
 public:
  // Submit ownership of the specified `spans` to the collector.  If the
  // collector delivers a response relevant to trace sampling, reconfigure the
  // sampler using the specified `response_handler`.  Return an error if one
  // occurs.
  virtual Expected<void> send(
      std::vector<std::unique_ptr<SpanData>>&& spans,
      const std::shared_ptr<TraceSampler>& response_handler) = 0;

  virtual ~Collector() {}
};

}  // namespace tracing
}  // namespace datadog
