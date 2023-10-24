#include "tracer.h"

#include <algorithm>
#include <cassert>

#include "datadog/runtime_id.h"
#include "datadog/trace_sampler_config.h"
#include "datadog_agent.h"
#include "dict_reader.h"
#include "environment.h"
#include "extracted_data.h"
#include "extraction_util.h"
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
#include "tracer_id.h"
#include "version.h"
#include "w3c_propagation.h"

namespace datadog {
namespace tracing {

Tracer::Tracer(const FinalizedTracerConfig& config)
    : Tracer(config, default_id_generator(config.trace_id_128_bit)) {}

Tracer::Tracer(const FinalizedTracerConfig& config,
               const std::shared_ptr<const IDGenerator>& generator)
    : logger_(config.logger),
      collector_(/* see constructor body */),
      defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      runtime_id_(config.runtime_id ? *config.runtime_id
                                    : RuntimeID::generate()),
      id_{runtime_id_, config.defaults.service, config.defaults.environment},
      tracer_telemetry_(std::make_shared<TracerTelemetry>(
          config.report_telemetry, config.clock, logger_, id_)),
      span_sampler_(
          std::make_shared<SpanSampler>(config.span_sampler, config.clock)),
      generator_(generator),
      clock_(config.clock),
      injection_styles_(config.injection_styles),
      extraction_styles_(config.extraction_styles),
      hostname_(config.report_hostname ? get_hostname() : nullopt),
      tags_header_max_size_(config.tags_header_size),
      config_manager_(config, config.clock) {
  if (auto* collector =
          std::get_if<std::shared_ptr<Collector>>(&config.collector)) {
    collector_ = *collector;
  } else {
    auto& agent_config =
        std::get<FinalizedDatadogAgentConfig>(config.collector);

    auto agent = std::make_shared<DatadogAgent>(
        agent_config, tracer_telemetry_, config.logger, id_, config_manager_);
    collector_ = agent;

    if (tracer_telemetry_->enabled()) {
      agent->send_app_started();
    }
  }

  if (config.log_on_startup) {
    logger_->log_startup([this](std::ostream& log) {
      log << "DATADOG TRACER CONFIGURATION - " << config_json();
    });
  }
}

nlohmann::json Tracer::config_json() const {
  // clang-format off
  auto config = nlohmann::json::object({
    {"version", tracer_version_string},
    {"defaults", to_json(*defaults_)},
    {"runtime_id", runtime_id_.string()},
    {"collector", collector_->config_json()},
    {"span_sampler", span_sampler_->config_json()},
    {"injection_styles", to_json(injection_styles_)},
    {"extraction_styles", to_json(extraction_styles_)},
    {"tags_header_size", tags_header_max_size_},
    {"environment_variables", environment::to_json()},
  });
  // clang-format on

  config.merge_patch(config_manager_.config_json());

  if (hostname_) {
    config["hostname"] = *hostname_;
  }

  return config;
}

Span Tracer::create_span() { return create_span(SpanConfig{}); }

Span Tracer::create_span(const SpanConfig& config) {
  auto span_data = std::make_unique<SpanData>();
  span_data->apply_config(*defaults_, config, clock_);
  span_data->trace_id = generator_->trace_id(span_data->start);
  span_data->span_id = span_data->trace_id.low;
  span_data->parent_id = 0;

  std::vector<std::pair<std::string, std::string>> trace_tags;
  if (span_data->trace_id.high) {
    trace_tags.emplace_back(tags::internal::trace_id_high,
                            hex_padded(span_data->trace_id.high));
  }

  const auto span_data_ptr = span_data.get();
  tracer_telemetry_->metrics().tracer.trace_segments_created_new.inc();
  const auto segment = std::make_shared<TraceSegment>(
      logger_, collector_, tracer_telemetry_,
      config_manager_.get_trace_sampler(), span_sampler_, defaults_,
      runtime_id_, injection_styles_, hostname_, nullopt /* origin */,
      tags_header_max_size_, std::move(trace_tags),
      nullopt /* sampling_decision */, nullopt /* additional_w3c_tracestate */,
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

  AuditedReader audited_reader{reader};

  auto span_data = std::make_unique<SpanData>();
  std::vector<ExtractedData> extracted_contexts;

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
        extract = &extract_none;
    }
    audited_reader.entries_found.clear();
    auto data = extract(audited_reader, span_data->tags, *logger_);
    if (auto* error = data.if_error()) {
      return error->with_prefix(
          extraction_error_prefix(style, audited_reader.entries_found));
    }
    extracted_contexts.push_back(std::move(*data));
    extracted_contexts.back().headers_examined = audited_reader.entries_found;
  }

  auto [trace_id, parent_id, origin, trace_tags, sampling_priority,
        additional_w3c_tracestate, additional_datadog_w3c_tracestate, style,
        headers_examined] = merge(extracted_contexts);

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
  // - if trace ID is zero, then that's an error.

  if (!trace_id && !parent_id) {
    return Error{Error::NO_SPAN_TO_EXTRACT,
                 "There's neither a trace ID nor a parent span ID to extract."}
        .with_prefix(extraction_error_prefix(style, headers_examined));
  }
  if (!trace_id) {
    std::string message;
    message +=
        "There's no trace ID to extract, but there is a parent span ID: ";
    message += std::to_string(*parent_id);
    return Error{Error::MISSING_TRACE_ID, std::move(message)}.with_prefix(
        extraction_error_prefix(style, headers_examined));
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
    return Error{Error::MISSING_PARENT_SPAN_ID, std::move(message)}.with_prefix(
        extraction_error_prefix(style, headers_examined));
  }

  if (!parent_id) {
    // We have a trace ID, but not parent ID.  We're meant to be the root, and
    // whoever called us already created a trace ID for us (to correlate with
    // whatever they're doing).
    parent_id = 0;
  }

  assert(parent_id);
  assert(trace_id);

  if (*trace_id == 0) {
    return Error{Error::ZERO_TRACE_ID,
                 "extracted zero value for trace ID, which is invalid"}
        .with_prefix(extraction_error_prefix(style, headers_examined));
  }

  // We're done extracting fields.  Now create the span.
  // This is similar to what we do in `create_span`.
  span_data->apply_config(*defaults_, config, clock_);
  span_data->span_id = generator_->span_id();
  span_data->trace_id = *trace_id;
  span_data->parent_id = *parent_id;

  if (span_data->trace_id.high) {
    // The trace ID has some bits set in the higher 64 bits. Set the
    // corresponding `trace_id_high` tag, so that the Datadog backend is aware
    // of those bits.
    //
    // First, though, if the `trace_id_high` tag is already set and has a
    // bogus value or a value inconsistent with the trace ID, tag an error.
    const auto hex_high = hex_padded(span_data->trace_id.high);
    const auto extant = std::find_if(
        trace_tags.begin(), trace_tags.end(), [&](const auto& pair) {
          return pair.first == tags::internal::trace_id_high;
        });
    if (extant == trace_tags.end()) {
      trace_tags.emplace_back(tags::internal::trace_id_high, hex_high);
    } else {
      // There is already a `trace_id_high` tag. `hex_high` is its proper
      // value. Check if the extant value is malformed or different from
      // `hex_high`. In either case, tag an error and overwrite the tag with
      // `hex_high`.
      const Optional<std::uint64_t> high = parse_trace_id_high(extant->second);
      if (!high) {
        span_data->tags[tags::internal::propagation_error] =
            "malformed_tid " + extant->second;
        extant->second = hex_high;
      } else if (*high != span_data->trace_id.high) {
        span_data->tags[tags::internal::propagation_error] =
            "inconsistent_tid " + extant->second;
        extant->second = hex_high;
      }
    }
  }

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
  tracer_telemetry_->metrics().tracer.trace_segments_created_continued.inc();
  const auto segment = std::make_shared<TraceSegment>(
      logger_, collector_, tracer_telemetry_,
      config_manager_.get_trace_sampler(), span_sampler_, defaults_,
      runtime_id_, injection_styles_, hostname_, std::move(origin),
      tags_header_max_size_, std::move(trace_tags),
      std::move(sampling_decision), std::move(additional_w3c_tracestate),
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
