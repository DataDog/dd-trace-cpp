#pragma once

#include <datadog/collector.h>
#include <datadog/span_data.h>

#include <vector>

#include "test.h"

using namespace datadog::tracing;

struct MockCollector : public Collector {
  std::vector<std::vector<std::unique_ptr<SpanData>>> chunks;

  Expected<void> send(std::vector<std::unique_ptr<SpanData>>&& spans,
                      const std::shared_ptr<TraceSampler>&) override {
    chunks.emplace_back(std::move(spans));
    return {};
  }

  SpanData& first_span() const {
    REQUIRE(chunks.size() == 1);
    const auto& chunk = chunks.front();
    REQUIRE(chunk.size() == 1);
    const auto& span_ptr = chunk.front();
    REQUIRE(span_ptr);
    return *span_ptr;
  }
};
