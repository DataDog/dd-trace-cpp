#include "tracer.h"

#include "datadog_agent.h"
#include "dict_reader.h"
#include "logger.h"
#include "parse_util.h"
#include "span.h"
#include "span_config.h"
#include "span_data.h"
#include "span_sampler.h"
#include "tag_propagation.h"
#include "tags.h"
#include "trace_sampler.h"
#include "trace_segment.h"

// for `::gethostname`
#ifdef _MSC_VER
#include <winsock.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cassert>

namespace datadog {
namespace tracing {
namespace {

std::optional<std::string> get_hostname() {
  char buffer[256];
  if (::gethostname(buffer, sizeof buffer)) {
    return std::nullopt;
  }
  return buffer;
}

class ExtractionPolicy {
 public:
  virtual Expected<std::optional<std::uint64_t>> trace_id(
      const DictReader& headers) = 0;
  virtual Expected<std::optional<std::uint64_t>> parent_id(
      const DictReader& headers) = 0;
  virtual Expected<std::optional<int>> sampling_priority(
      const DictReader& headers) = 0;
  virtual std::optional<std::string> origin(const DictReader& headers) = 0;
  virtual std::optional<std::string> trace_tags(const DictReader&) = 0;
};

class DatadogExtractionPolicy : public ExtractionPolicy {
  Expected<std::optional<std::uint64_t>> id(const DictReader& headers,
                                            std::string_view header,
                                            std::string_view kind) {
    auto found = headers.lookup(header);
    if (!found) {
      return std::nullopt;
    }
    auto result = parse_uint64(*found, 10);
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Could not extract Datadog-style ";
      prefix += kind;
      prefix += "ID from ";
      prefix += header;
      prefix += ": ";
      prefix += *found;
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return *result;
  }

 public:
  Expected<std::optional<std::uint64_t>> trace_id(
      const DictReader& headers) override {
    return id(headers, "x-datadog-trace-id", "trace");
  }

  Expected<std::optional<std::uint64_t>> parent_id(
      const DictReader& headers) override {
    return id(headers, "x-datadog-parent-id", "parent span");
  }

  Expected<std::optional<int>> sampling_priority(
      const DictReader& headers) override {
    const std::string_view header = "x-datadog-sampling-priority";
    auto found = headers.lookup(header);
    if (!found) {
      return std::nullopt;
    }
    auto result = parse_int(*found, 10);
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Could not extract Datadog-style sampling priority from ";
      prefix += header;
      prefix += ": ";
      prefix += *found;
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return *result;
  }

  std::optional<std::string> origin(const DictReader& headers) override {
    auto found = headers.lookup("x-datadog-origin");
    if (found) {
      return std::string(*found);
    }
    return std::nullopt;
  }

  std::optional<std::string> trace_tags(const DictReader& headers) override {
    auto found = headers.lookup("x-datadog-tags");
    if (found) {
      return std::string(*found);
    }
    return std::nullopt;
  }
};

class B3ExtractionPolicy : public DatadogExtractionPolicy {
  Expected<std::optional<std::uint64_t>> id(const DictReader& headers,
                                            std::string_view header,
                                            std::string_view kind) {
    auto found = headers.lookup(header);
    if (!found) {
      return std::nullopt;
    }
    auto result = parse_uint64(*found, 16);
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Could not extract B3-style ";
      prefix += kind;
      prefix += "ID from ";
      prefix += header;
      prefix += ": ";
      prefix += *found;
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return *result;
  }

 public:
  Expected<std::optional<std::uint64_t>> trace_id(
      const DictReader& headers) override {
    return id(headers, "x-b3-traceid", "trace");
  }

  Expected<std::optional<std::uint64_t>> parent_id(
      const DictReader& headers) override {
    return id(headers, "x-b3-spanid", "parent span");
  }

  Expected<std::optional<int>> sampling_priority(
      const DictReader& headers) override {
    const std::string_view header = "x-b3-sampled";
    auto found = headers.lookup(header);
    if (!found) {
      return std::nullopt;
    }
    auto result = parse_int(*found, 10);
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Could not extract B3-style sampling priority from ";
      prefix += header;
      prefix += ": ";
      prefix += *found;
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return *result;
  }
};

class W3CExtractionPolicy : public DatadogExtractionPolicy {
  // TODO
};

struct ExtractedData {
  std::optional<std::uint64_t> trace_id;
  std::optional<std::uint64_t> parent_id;
  std::optional<std::string> origin;
  std::optional<std::string> trace_tags;
  std::optional<int> sampling_priority;
};

bool operator!=(const ExtractedData& left, const ExtractedData& right) {
  return left.trace_id != right.trace_id || left.parent_id != right.parent_id ||
         left.origin != right.origin || left.trace_tags != right.trace_tags ||
         left.sampling_priority != right.sampling_priority;
}

Expected<ExtractedData> extract_data(ExtractionPolicy& extract,
                                     const DictReader& reader) {
  ExtractedData extracted_data;

  auto& [trace_id, parent_id, origin, trace_tags, sampling_priority] = extracted_data;

  auto maybe_trace_id = extract.trace_id(reader);
  if (auto* error = maybe_trace_id.if_error()) {
    return std::move(*error);
  }
  trace_id = *maybe_trace_id;

  origin = extract.origin(reader);

  auto maybe_parent_id = extract.parent_id(reader);
  if (auto* error = maybe_parent_id.if_error()) {
    return std::move(*error);
  }
  parent_id = *maybe_parent_id;

  auto maybe_sampling_priority = extract.sampling_priority(reader);
  if (auto* error = maybe_sampling_priority.if_error()) {
    return std::move(*error);
  }
  sampling_priority = *maybe_sampling_priority;

  trace_tags = extract.trace_tags(reader);

  return extracted_data;
}

}  // namespace

Tracer::Tracer(const FinalizedTracerConfig& config)
    : Tracer(config, default_id_generator, default_clock) {}

Tracer::Tracer(const FinalizedTracerConfig& config,
               const IDGenerator& generator, const Clock& clock)
    : logger_(config.logger),
      collector_(/* see constructor body */),
      trace_sampler_(
          std::make_shared<TraceSampler>(config.trace_sampler, clock)),
      span_sampler_(std::make_shared<SpanSampler>(config.span_sampler, clock)),
      generator_(generator),
      clock_(clock),
      defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      injection_styles_(config.injection_styles),
      extraction_styles_(config.extraction_styles),
      hostname_(config.report_hostname ? get_hostname() : std::nullopt),
      tags_header_max_size_(config.tags_header_size) {
  if (auto* collector =
          std::get_if<std::shared_ptr<Collector>>(&config.collector)) {
    collector_ = *collector;
  } else {
    auto& agent_config =
        std::get<FinalizedDatadogAgentConfig>(config.collector);
    collector_ =
        std::make_shared<DatadogAgent>(agent_config, clock, config.logger);
  }

  if (config.log_on_startup) {
    logger_->log_startup(
        [](auto& stream) { stream << "TODO: Here is your startup message."; });
  }
}

Span Tracer::create_span() {
  return create_span(SpanConfig{});
}

Span Tracer::create_span(const SpanConfig& config) {
  auto span_data = std::make_unique<SpanData>();
  span_data->apply_config(*defaults_, config, clock_);
  span_data->span_id = generator_.generate_span_id();
  span_data->trace_id = span_data->span_id;
  span_data->parent_id = 0;

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      logger_, collector_, trace_sampler_, span_sampler_, defaults_,
      injection_styles_, hostname_, std::nullopt /* origin */,
      tags_header_max_size_,
      std::unordered_map<std::string, std::string>{} /* trace_tags */,
      std::nullopt /* sampling_decision */, std::move(span_data));
  Span span{span_data_ptr, segment, generator_.generate_span_id, clock_};
  return span;
}

Expected<Span> Tracer::extract_span(const DictReader& reader) {
  return extract_span(reader, SpanConfig{});
}

Expected<Span> Tracer::extract_span(const DictReader& reader,
                                    const SpanConfig& config) {
  // TODO: I can assume this because of the current config validator.
  assert((extraction_styles_.datadog || extraction_styles_.b3) &&
         !extraction_styles_.w3c);
  // end TODO

  std::optional<ExtractedData> extracted_data;
  const char* extracted_by;

  if (extraction_styles_.datadog) {
    DatadogExtractionPolicy extract;
    auto data = extract_data(extract, reader);
    if (auto* error = data.if_error()) {
      return std::move(*error);
    }
    extracted_data = *data;
    extracted_by = "Datadog";
  }

  if (extraction_styles_.b3) {
    B3ExtractionPolicy extract;
    auto data = extract_data(extract, reader);
    if (auto* error = data.if_error()) {
      return std::move(*error);
    }
    if (extracted_data && *data != *extracted_data) {
      std::string message;
      message += "B3 extracted different data than did ";
      message += extracted_by;
      // TODO: diagnose difference
      return Error{Error::INCONSISTENT_EXTRACTION_STYLES, std::move(message)};
    }
    extracted_data = *data;
    extracted_by = "B3";
  }

  if (extraction_styles_.w3c) {
    W3CExtractionPolicy extract;
    auto data = extract_data(extract, reader);
    if (auto* error = data.if_error()) {
      return std::move(*error);
    }
    if (extracted_data && *data != *extracted_data) {
      std::string message;
      message += "W3C extracted different data than did ";
      message += extracted_by;
      // TODO: diagnose difference
      return Error{Error::INCONSISTENT_EXTRACTION_STYLES, std::move(message)};
    }
    extracted_data = *data;
    extracted_by = "W3C";
  }

  assert(extracted_data);
  auto& [trace_id, parent_id, origin, trace_tags, sampling_priority] = *extracted_data;

  // Some information might be missing.
  // Here are the combinations considered:
  //
  // - no trace ID and no parent ID
  //     - this means there's no span to extract
  // - parent ID and no trace ID
  //     - error
  // - trace ID and no parent ID
  //     - if origin is set, then we're extracting a root span
  //         - the idea is that "synthetics" might have started a trace without
  //           producing a root span
  //     - if origin is _not_ set, then it's an error
  // - trace ID and parent ID means we're extracting a child span
  // - parent ID without trace ID is an error

  if (!trace_id && !parent_id) {
    return Error{Error::NO_SPAN_TO_EXTRACT,
                 "There's trace ID or parent span ID to extract."};
  }
  if (!trace_id) {
    std::string message;
    message +=
        "There's no trace ID to extract, but there is a parent span ID: ";
    message += std::to_string(*parent_id);
    return Error{Error::MISSING_TRACE_ID, std::move(message)};
  }
  if (!parent_id && !origin) {
    std::string message;
    message +=
        "There's no parent span ID to extract, but there is a trace ID: ";
    message += std::to_string(*trace_id);
    return Error{Error::MISSING_PARENT_SPAN_ID, std::move(message)};
  }

  if (!parent_id) {
    // We have a trace ID, but not parent ID.  We're meant to be the root, and
    // whoever called us already created a trace ID for us (to correlate with
    // whatever they're doing).
    parent_id = 0;
  }

  // We're done extracting fields.  Now create the span.
  // This is similar to what we do in `create_span`.
  assert(parent_id);
  assert(trace_id);

  auto span_data = std::make_unique<SpanData>();
  span_data->apply_config(*defaults_, config, clock_);
  span_data->span_id = generator_.generate_span_id();
  span_data->trace_id = *trace_id;
  span_data->parent_id = *parent_id;

  std::optional<SamplingDecision> sampling_decision;
  if (sampling_priority) {
    SamplingDecision decision;
    decision.priority = *sampling_priority;
    // `decision.mechanism` is null.  We might be able to infer it once we
    // extract `trace_tags`, but we would have no use for it, so we won't.
    decision.origin = SamplingDecision::Origin::EXTRACTED;

    sampling_decision = decision;
  }

  std::unordered_map<std::string, std::string> decoded_trace_tags;
  if (trace_tags) {
    auto maybe_trace_tags = decode_tags(*trace_tags);
    if (auto* error = maybe_trace_tags.if_error()) {
      logger_->log_error(*error);
      span_data->tags[tags::internal::propagation_error] = "decoding_error";
    } else {
      decoded_trace_tags = std::move(*maybe_trace_tags);
    }
  }

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      logger_, collector_, trace_sampler_, span_sampler_, defaults_,
      injection_styles_, hostname_, std::move(origin), tags_header_max_size_,
      std::move(decoded_trace_tags), std::move(sampling_decision),
      std::move(span_data));
  Span span{span_data_ptr, segment, generator_.generate_span_id, clock_};
  return span;
}

Expected<Span> Tracer::extract_or_create_span(const DictReader& reader) {
  return extract_or_create_span(reader, SpanConfig{});
}

Expected<Span> Tracer::extract_or_create_span(const DictReader& reader,
                                              const SpanConfig& config) {
  auto maybe_span = extract_span(reader, config);
  if (auto* error = maybe_span.if_error()) {
    // If the error is `NO_SPAN_TO_EXTRACT`, then fine, we'll create a span
    // instead.
    // If, however, there was some other error, then return the error.
    if (error->code != Error::NO_SPAN_TO_EXTRACT) {
      return maybe_span;
    }
  }

  return create_span(config);
}

}  // namespace tracing
}  // namespace datadog
