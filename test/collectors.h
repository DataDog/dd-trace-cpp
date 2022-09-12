#pragma once

#include <datadog/collector.h>
#include <datadog/span_data.h>

#include <vector>

using namespace datadog::tracing;

struct MockCollector : public Collector {
  std::vector<std::vector<std::unique_ptr<SpanData>>> chunks;

  Expected<void> send(
      std::vector<std::unique_ptr<SpanData>>&& spans,
      const std::shared_ptr<TraceSampler>&) override {
        chunks.emplace_back(std::move(spans));
        return {};
      }
};
