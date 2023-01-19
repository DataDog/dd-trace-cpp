#include "tracer.h"

#include <algorithm>
#include <cassert>

#include "datadog_agent.h"
#include "dict_reader.h"
#include "environment.h"
#include "extracted_data.h"
#include "hex.h"
#include "json.hpp"
#include "logger.h"
#include "parse_util.h"
#include "platform_util.h"
#include "span.h"
#include "span_config.h"
#include "span_data.h"
#include "span_sampler.h"
#include "tag_propagation.h"
#include "tags.h"
#include "trace_sampler.h"
#include "trace_segment.h"
#include "version.h"
#include "w3c_propagation.h"

namespace datadog {
namespace tracing {
namespace {

// Decode the specified `trace_tags` and integrate them into the specified
// `result`. If an error occurs, add a `tags::internal::propagation_error` tag
// to the specified `span_tags` and log a diagnostic using the specified
// `logger`.
void handle_trace_tags(StringView trace_tags, ExtractedData& result,
                       std::unordered_map<std::string, std::string>& span_tags,
                       Logger& logger) {
  auto maybe_trace_tags = decode_tags(trace_tags);
  if (auto* error = maybe_trace_tags.if_error()) {
    logger.log_error(*error);
    span_tags[tags::internal::propagation_error] = "decoding_error";
    return;
  }

  for (auto& [key, value] : *maybe_trace_tags) {
    if (!starts_with(key, "_dd.p.")) {
      continue;
    }

    if (key == tags::internal::trace_id_high) {
      // _dd.p.tid contains the high 64 bits of the trace ID.
      auto high = parse_uint64(value, 16);
      if (auto* error = high.if_error()) {
        logger.log_error(
            error->with_prefix("Unable to parse high bits of the trace ID in "
                               "Datadog style from the "
                               "\"_dd.p.tid\" trace tag: "));
        span_tags[tags::internal::propagation_error] = "decoding_error";
      }
      // Note that this assumes the lower 64 bits of the trace ID have already
      // been extracted (i.e. we look for X-Datadog-Trace-ID first).
      if (result.trace_id) {
        result.trace_id->high = *high;
      }
    }

    result.trace_tags.emplace_back(std::move(key), std::move(value));
  }
}

Expected<Optional<std::uint64_t>> extract_id_header(const DictReader& headers,
                                                    StringView header,
                                                    StringView header_kind,
                                                    StringView style_name,
                                                    int base) {
  auto found = headers.lookup(header);
  if (!found) {
    return nullopt;
  }
  auto result = parse_uint64(*found, base);
  if (auto* error = result.if_error()) {
    std::string prefix;
    prefix += "Could not extract ";
    append(prefix, style_name);
    prefix += "-style ";
    append(prefix, header_kind);
    prefix += "ID from ";
    append(prefix, header);
    prefix += ": ";
    append(prefix, *found);
    prefix += ' ';
    return error->with_prefix(prefix);
  }
  return *result;
}

Expected<ExtractedData> extract_datadog(
    const DictReader& headers,
    std::unordered_map<std::string, std::string>& span_tags, Logger& logger) {
  ExtractedData result;

  auto trace_id =
      extract_id_header(headers, "x-datadog-trace-id", "trace", "Datadog", 10);
  if (auto* error = trace_id.if_error()) {
    return std::move(*error);
  }
  if (*trace_id) {
    result.trace_id = TraceID(**trace_id);
  }

  auto parent_id = extract_id_header(headers, "x-datadog-parent-id",
                                     "parent span", "Datadog", 10);
  if (auto* error = parent_id.if_error()) {
    return std::move(*error);
  }
  result.parent_id = *parent_id;

  const StringView sampling_priority_header = "x-datadog-sampling-priority";
  if (auto found = headers.lookup(sampling_priority_header)) {
    auto sampling_priority = parse_int(*found, 10);
    if (auto* error = sampling_priority.if_error()) {
      std::string prefix;
      prefix += "Could not extract Datadog-style sampling priority from ";
      append(prefix, sampling_priority_header);
      prefix += ": ";
      append(prefix, *found);
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    result.sampling_priority = *sampling_priority;
  }

  auto origin = headers.lookup("x-datadog-origin");
  if (origin) {
    result.origin = std::string(*origin);
  }

  auto trace_tags = headers.lookup("x-datadog-tags");
  if (trace_tags) {
    handle_trace_tags(*trace_tags, result, span_tags, logger);
  }

  return result;
}

Expected<ExtractedData> extract_b3(
    const DictReader& headers, std::unordered_map<std::string, std::string>&,
    Logger&) {
  ExtractedData result;

  if (auto found = headers.lookup("x-b3-traceid")) {
    auto parsed = TraceID::parse_hex(*found);
    if (auto* error = parsed.if_error()) {
      std::string prefix = "Could not extract B3-style trace ID from \"";
      append(prefix, *found);
      prefix += "\": ";
      return error->with_prefix(prefix);
    }
    result.trace_id = *parsed;
  }

  auto parent_id =
      extract_id_header(headers, "x-b3-spanid", "parent span", "B3", 16);
  if (auto* error = parent_id.if_error()) {
    return std::move(*error);
  }
  result.parent_id = *parent_id;

  const StringView sampling_priority_header = "x-b3-sampled";
  if (auto found = headers.lookup(sampling_priority_header)) {
    auto sampling_priority = parse_int(*found, 10);
    if (auto* error = sampling_priority.if_error()) {
      std::string prefix;
      prefix += "Could not extract B3-style sampling priority from ";
      append(prefix, sampling_priority_header);
      prefix += ": ";
      append(prefix, *found);
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    result.sampling_priority = *sampling_priority;
  }

  return result;
}

void log_startup_message(Logger& logger, StringView tracer_version_string,
                         const Collector& collector,
                         const SpanDefaults& defaults,
                         const TraceSampler& trace_sampler,
                         const SpanSampler& span_sampler,
                         const std::vector<PropagationStyle>& injection_styles,
                         const std::vector<PropagationStyle>& extraction_styles,
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
    : Tracer(config, default_id_generator(config.trace_id_128_bit),
             default_clock) {}

Tracer::Tracer(const FinalizedTracerConfig& config,
               const std::shared_ptr<const IDGenerator>& generator)
    : Tracer(config, generator, default_clock) {}

Tracer::Tracer(const FinalizedTracerConfig& config, const Clock& clock)
    : Tracer(config, default_id_generator(config.trace_id_128_bit), clock) {}

Tracer::Tracer(const FinalizedTracerConfig& config,
               const std::shared_ptr<const IDGenerator>& generator,
               const Clock& clock)
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
  std::vector<std::pair<std::string, std::string>> trace_tags;
  span_data->trace_id = generator_->trace_id();
  if (span_data->trace_id.high) {
    trace_tags.emplace_back(tags::internal::trace_id_high,
                            hex(span_data->trace_id.high));
  }
  span_data->span_id = span_data->trace_id.low;
  span_data->parent_id = 0;

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      logger_, collector_, trace_sampler_, span_sampler_, defaults_,
      injection_styles_, hostname_, nullopt /* origin */, tags_header_max_size_,
      std::move(trace_tags), nullopt /* sampling_decision */,
      nullopt /* additional_w3c_tracestate */,
      nullopt /* additional_datadog_w3c_tracestate*/, std::move(span_data));
  Span span{span_data_ptr, segment,
            [generator = generator_]() { return generator->span_id(); },
            clock_};
  return span;
}

Expected<Span> Tracer::extract_span(const DictReader& reader) {
  return extract_span(reader, SpanConfig{});
}

Expected<Span> Tracer::extract_span(const DictReader& reader,
                                    const SpanConfig& config) {
  assert(!extraction_styles_.empty());

  auto span_data = std::make_unique<SpanData>();
  ExtractedData extracted_data;

  for (const auto style : extraction_styles_) {
    using Extractor = decltype(&extract_datadog);  // function pointer
    Extractor extract;
    switch (style) {
      case PropagationStyle::DATADOG:
        extract = &extract_datadog;
        break;
      case PropagationStyle::B3:
        extract = &extract_b3;
        break;
      case PropagationStyle::W3C:
        extract = &extract_w3c;
        break;
      default:
        assert(style == PropagationStyle::NONE);
        extracted_data = ExtractedData{};
        continue;
    }
    auto data = extract(reader, span_data->tags, *logger_);
    if (auto* error = data.if_error()) {
      return std::move(*error);
    }
    extracted_data = *data;
    // If the extractor produced a non-null trace ID, then we consider this
    // extraction style the one "chosen" for this trace.
    // Otherwise, we loop around to the next configured extraction style.
    if (extracted_data.trace_id) {
      break;
    }
  }

  auto& [trace_id, parent_id, origin, trace_tags, sampling_priority,
         additional_w3c_tracestate, additional_datadog_w3c_tracestate] =
      extracted_data;

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
    message += "[hexadecimal = ";
    message += trace_id->hex_padded();
    if (trace_id->high == 0) {
      message += ", decimal = ";
      message += std::to_string(trace_id->low);
    }
    message += ']';
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

  span_data->apply_config(*defaults_, config, clock_);
  span_data->span_id = generator_->span_id();
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

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      logger_, collector_, trace_sampler_, span_sampler_, defaults_,
      injection_styles_, hostname_, std::move(origin), tags_header_max_size_,
      std::move(trace_tags), std::move(sampling_decision),
      std::move(additional_w3c_tracestate),
      std::move(additional_datadog_w3c_tracestate), std::move(span_data));
  Span span{span_data_ptr, segment,
            [generator = generator_]() { return generator->span_id(); },
            clock_};
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
