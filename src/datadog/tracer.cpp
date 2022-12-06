#include "tracer.h"

#include <algorithm>
#include <cassert>

#include "datadog_agent.h"
#include "dict_reader.h"
#include "environment.h"
#include "json.hpp"
#include "logger.h"
#include "net_util.h"
#include "parse_util.h"
#include "span.h"
#include "span_config.h"
#include "span_data.h"
#include "span_sampler.h"
#include "tag_propagation.h"
#include "tags.h"
#include "trace_sampler.h"
#include "trace_segment.h"
#include "version.h"

namespace datadog {
namespace tracing {
namespace {

class ExtractionPolicy {
 public:
  virtual Expected<Optional<std::uint64_t>> trace_id(
      const DictReader& headers) = 0;
  virtual Expected<Optional<std::uint64_t>> parent_id(
      const DictReader& headers) = 0;
  virtual Expected<Optional<int>> sampling_priority(
      const DictReader& headers) = 0;
  virtual Optional<std::string> origin(const DictReader& headers) = 0;
  virtual Optional<std::string> trace_tags(const DictReader&) = 0;
};

class DatadogExtractionPolicy : public ExtractionPolicy {
  Expected<Optional<std::uint64_t>> id(const DictReader& headers,
                                       StringView header, StringView kind) {
    auto found = headers.lookup(header);
    if (!found) {
      return nullopt;
    }
    auto result = parse_uint64(*found, 10);
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Could not extract Datadog-style ";
      append(prefix, kind);
      prefix += "ID from ";
      append(prefix, header);
      prefix += ": ";
      append(prefix, *found);
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return *result;
  }

 public:
  Expected<Optional<std::uint64_t>> trace_id(
      const DictReader& headers) override {
    return id(headers, "x-datadog-trace-id", "trace");
  }

  Expected<Optional<std::uint64_t>> parent_id(
      const DictReader& headers) override {
    return id(headers, "x-datadog-parent-id", "parent span");
  }

  Expected<Optional<int>> sampling_priority(
      const DictReader& headers) override {
    const StringView header = "x-datadog-sampling-priority";
    auto found = headers.lookup(header);
    if (!found) {
      return nullopt;
    }
    auto result = parse_int(*found, 10);
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Could not extract Datadog-style sampling priority from ";
      append(prefix, header);
      prefix += ": ";
      append(prefix, *found);
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return *result;
  }

  Optional<std::string> origin(const DictReader& headers) override {
    auto found = headers.lookup("x-datadog-origin");
    if (found) {
      return std::string(*found);
    }
    return nullopt;
  }

  Optional<std::string> trace_tags(const DictReader& headers) override {
    auto found = headers.lookup("x-datadog-tags");
    if (found) {
      return std::string(*found);
    }
    return nullopt;
  }
};

class B3ExtractionPolicy : public DatadogExtractionPolicy {
  Expected<Optional<std::uint64_t>> id(const DictReader& headers,
                                       StringView header, StringView kind) {
    auto found = headers.lookup(header);
    if (!found) {
      return nullopt;
    }
    auto result = parse_uint64(*found, 16);
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Could not extract B3-style ";
      append(prefix, kind);
      prefix += "ID from ";
      append(prefix, header);
      prefix += ": ";
      append(prefix, *found);
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return *result;
  }

 public:
  Expected<Optional<std::uint64_t>> trace_id(
      const DictReader& headers) override {
    return id(headers, "x-b3-traceid", "trace");
  }

  Expected<Optional<std::uint64_t>> parent_id(
      const DictReader& headers) override {
    return id(headers, "x-b3-spanid", "parent span");
  }

  Expected<Optional<int>> sampling_priority(
      const DictReader& headers) override {
    const StringView header = "x-b3-sampled";
    auto found = headers.lookup(header);
    if (!found) {
      return nullopt;
    }
    auto result = parse_int(*found, 10);
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Could not extract B3-style sampling priority from ";
      append(prefix, header);
      prefix += ": ";
      append(prefix, *found);
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return *result;
  }
};

struct ExtractedData {
  Optional<std::uint64_t> trace_id;
  Optional<std::uint64_t> parent_id;
  Optional<std::string> origin;
  Optional<std::string> trace_tags;
  Optional<int> sampling_priority;
};

bool operator!=(const ExtractedData& left, const ExtractedData& right) {
  return left.trace_id != right.trace_id || left.parent_id != right.parent_id ||
         left.origin != right.origin || left.trace_tags != right.trace_tags ||
         left.sampling_priority != right.sampling_priority;
}

Expected<ExtractedData> extract_data(ExtractionPolicy& extract,
                                     const DictReader& reader) {
  ExtractedData extracted_data;

  auto& [trace_id, parent_id, origin, trace_tags, sampling_priority] =
      extracted_data;

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

void log_startup_message(Logger& logger, StringView tracer_version_string,
                         const Collector& collector,
                         const SpanDefaults& defaults,
                         const TraceSampler& trace_sampler,
                         const SpanSampler& span_sampler,
                         const PropagationStyles& injection_styles,
                         const PropagationStyles& extraction_styles,
                         const Optional<std::string>& hostname,
                         std::size_t tags_header_max_size) {
  // clang-format off
  auto config = nlohmann::json::object({
    {"version", tracer_version_string},
    {"defaults", to_json(defaults)},
    {"collector", collector.config_json()},
    {"trace_sampler", trace_sampler.config_json()},
    {"span_sampler", span_sampler.config_json()},
    {"injection_styles", to_json(injection_styles)},
    {"extraction_styles", to_json(extraction_styles)},
    {"tags_header_size", tags_header_max_size},
    {"environment_variables", environment::to_json()},
  });
  // clang-format on

  if (hostname) {
    config["hostname"] = *hostname;
  }

  logger.log_startup([&config](std::ostream& log) {
    log << "DATADOG TRACER CONFIGURATION - " << config;
  });
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
      hostname_(config.report_hostname ? get_hostname() : nullopt),
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
    log_startup_message(*logger_, tracer_version_string, *collector_,
                        *defaults_, *trace_sampler_, *span_sampler_,
                        injection_styles_, extraction_styles_, hostname_,
                        tags_header_max_size_);
  }
}

Span Tracer::create_span() { return create_span(SpanConfig{}); }

Span Tracer::create_span(const SpanConfig& config) {
  auto span_data = std::make_unique<SpanData>();
  span_data->apply_config(*defaults_, config, clock_);
  span_data->span_id = generator_();
  span_data->trace_id = span_data->span_id;
  span_data->parent_id = 0;

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      logger_, collector_, trace_sampler_, span_sampler_, defaults_,
      injection_styles_, hostname_, nullopt /* origin */,
      tags_header_max_size_,
      std::unordered_map<std::string, std::string>{} /* trace_tags */,
      nullopt /* sampling_decision */, std::move(span_data));
  Span span{span_data_ptr, segment, generator_, clock_};
  return span;
}

Expected<Span> Tracer::extract_span(const DictReader& reader) {
  return extract_span(reader, SpanConfig{});
}

Expected<Span> Tracer::extract_span(const DictReader& reader,
                                    const SpanConfig& config) {
  assert(extraction_styles_.datadog || extraction_styles_.b3);

  Optional<ExtractedData> extracted_data;
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

  assert(extracted_data);
  auto& [trace_id, parent_id, origin, trace_tags, sampling_priority] =
      *extracted_data;

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
                 "There's neither a trace ID nor a parent span ID to extract."};
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
  span_data->span_id = generator_();
  span_data->trace_id = *trace_id;
  span_data->parent_id = *parent_id;

  Optional<SamplingDecision> sampling_decision;
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
      for (const auto& [key, value] : *maybe_trace_tags) {
        if (starts_with(key, "_dd.p.")) {
          decoded_trace_tags.insert_or_assign(key, value);
        }
      }
    }
  }

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      logger_, collector_, trace_sampler_, span_sampler_, defaults_,
      injection_styles_, hostname_, std::move(origin), tags_header_max_size_,
      std::move(decoded_trace_tags), std::move(sampling_decision),
      std::move(span_data));
  Span span{span_data_ptr, segment, generator_, clock_};
  return span;
}

Expected<Span> Tracer::extract_or_create_span(const DictReader& reader) {
  return extract_or_create_span(reader, SpanConfig{});
}

Expected<Span> Tracer::extract_or_create_span(const DictReader& reader,
                                              const SpanConfig& config) {
  auto maybe_span = extract_span(reader, config);
  if (!maybe_span && maybe_span.error().code == Error::NO_SPAN_TO_EXTRACT) {
    return create_span(config);
  }
  return maybe_span;
}

}  // namespace tracing
}  // namespace datadog
