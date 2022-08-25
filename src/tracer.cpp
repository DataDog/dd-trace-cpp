#include "tracer.h"

#include "datadog_agent.h"
#include "dict_reader.h"
#include "span.h"
#include "span_config.h"
#include "span_data.h"
#include "span_sampler.h"
#include "tags.h"
#include "trace_sampler.h"
#include "trace_segment.h"

// for `::gethostname`
#ifdef _MSC_VER
#include <winsock.h>
#else
#include <unistd.h>
#endif

#include <cassert>
#include <charconv>  // for `std::from_chars`
#include <iostream>  // TODO: no

namespace datadog {
namespace tracing {
namespace {

std::optional<std::string> get_hostname() {
  char buffer[256];
  if (::gethostname(buffer, sizeof buffer)) {
    // TODO: log an error?
    return std::nullopt;
  }
  return buffer;
}

template <typename Integer>
std::variant<Integer, Error> parse_integer(std::string_view input, int base,
                                           std::string_view kind) {
  Integer value;
  const auto status = std::from_chars(input.begin(), input.end(), value, base);
  if (status.ec == std::errc::invalid_argument) {
    std::string message;
    message += "Is not a valid integer: \"";
    message += input;
    message += '\"';
    return Error{Error::INVALID_INTEGER, std::move(message)};
  } else if (status.ptr != input.end()) {
    std::string message;
    message += "Integer has trailing characters in: \"";
    message += input;
    message += '\"';
    return Error{Error::INVALID_INTEGER, std::move(message)};
  } else if (status.ec == std::errc::result_out_of_range) {
    std::string message;
    message += "Integer is not within the range of ";
    message += kind;
    message += ": ";
    message += input;
    return Error{Error::OUT_OF_RANGE_INTEGER, std::move(message)};
  }
  return value;
}

std::variant<std::uint64_t, Error> parse_uint64(std::string_view input,
                                                int base) {
  return parse_integer<std::uint64_t>(input, base, "64-bit unsigned");
}

std::variant<int, Error> parse_int(std::string_view input, int base) {
  return parse_integer<int>(input, base, "int");
}

class ExtractionPolicy {
 public:
  virtual std::variant<std::optional<std::uint64_t>, Error> trace_id(
      const DictReader& headers) = 0;
  virtual std::variant<std::optional<std::uint64_t>, Error> parent_id(
      const DictReader& headers) = 0;
  virtual std::variant<std::optional<int>, Error> sampling_priority(
      const DictReader& headers) = 0;
  virtual std::optional<std::string> origin(const DictReader& headers) = 0;
  virtual std::variant<std::unordered_map<std::string, std::string>, Error>
  trace_tags(const DictReader&) = 0;
};

class DatadogExtractionPolicy : public ExtractionPolicy {
  std::variant<std::optional<std::uint64_t>, Error> id(
      const DictReader& headers, std::string_view header,
      std::string_view kind) {
    auto found = headers.lookup(header);
    if (!found) {
      return std::nullopt;
    }
    auto result = parse_uint64(*found, 10);
    if (auto* error = std::get_if<Error>(&result)) {
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
    return std::get<std::uint64_t>(result);
  }

 public:
  std::variant<std::optional<std::uint64_t>, Error> trace_id(
      const DictReader& headers) override {
    return id(headers, "x-datadog-trace-id", "trace");
  }

  std::variant<std::optional<std::uint64_t>, Error> parent_id(
      const DictReader& headers) override {
    return id(headers, "x-datadog-parent-id", "parent span");
  }

  std::variant<std::optional<int>, Error> sampling_priority(
      const DictReader& headers) override {
    const std::string_view header = "x-datadog-sampling-priority";
    auto found = headers.lookup(header);
    if (!found) {
      return std::nullopt;
    }
    auto result = parse_int(*found, 10);
    if (auto* error = std::get_if<Error>(&result)) {
      std::string prefix;
      prefix += "Could not extract Datadog-style sampling priority from ";
      prefix += header;
      prefix += ": ";
      prefix += *found;
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    return std::get<int>(result);
  }

  std::optional<std::string> origin(const DictReader& headers) override {
    auto found = headers.lookup("x-datadog-origin");
    if (found) {
      return std::string(*found);
    }
    return std::nullopt;
  }

  std::variant<std::unordered_map<std::string, std::string>, Error> trace_tags(
      const DictReader& headers) override {
    auto found = headers.lookup("x-datadog-tags");
    if (!found) {
      return std::unordered_map<std::string, std::string>{};
    }
    // TODO
    std::string message;
    message += "Tag propagation is not implemented yet, so I won't parse: \"";
    message += *found;
    message += '\"';
    return Error{Error::NOT_IMPLEMENTED, std::move(message)};
  }
};

}  // namespace

Tracer::Tracer(const Validated<TracerConfig>& config)
    : Tracer(config, default_id_generator, default_clock) {}

Tracer::Tracer(const Validated<TracerConfig>& config,
               const IDGenerator& generator, const Clock& clock)
    : collector_(/* see constructor body */),
      trace_sampler_(std::make_shared<TraceSampler>(
          bless(&TracerConfig::trace_sampler, config))),
      span_sampler_(std::make_shared<SpanSampler>(
          bless(&TracerConfig::span_sampler, config))),
      generator_(generator),
      clock_(clock),
      defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      injection_styles_(config.injection_styles),
      extraction_styles_(config.extraction_styles),
      hostname_(config.report_hostname ? get_hostname() : std::nullopt) {
  if (auto* collector =
          std::get_if<std::shared_ptr<Collector>>(&config.collector)) {
    collector_ = *collector;
  } else {
    collector_ =
        std::make_shared<DatadogAgent>(bless(&TracerConfig::collector, config));
  }
}

Span Tracer::create_span(const SpanConfig& config) {
  auto span_data = std::make_unique<SpanData>();
  span_data->apply_config(*defaults_, config, clock_);
  span_data->span_id = generator_.generate_span_id();
  span_data->trace_id = span_data->span_id;
  span_data->parent_id = 0;

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      collector_, trace_sampler_, span_sampler_, defaults_, injection_styles_,
      hostname_, std::nullopt /* origin */,
      std::unordered_map<std::string, std::string>{} /* trace_tags */,
      std::nullopt /* sampling_decision */, std::move(span_data));
  Span span{span_data_ptr, segment, generator_.generate_span_id, clock_};
  return span;
}

std::variant<Span, Error> Tracer::extract_span(const DictReader& reader) {
  return extract_span(reader, SpanConfig{});
}

std::variant<Span, Error> Tracer::extract_span(const DictReader& reader,
                                               const SpanConfig& config) {
  // TODO: I can assume this because of the current config validator.
  assert(extraction_styles_.datadog && !extraction_styles_.b3 &&
         !extraction_styles_.w3c);
  // end TODO

  std::optional<std::uint64_t> trace_id;
  std::optional<std::uint64_t> parent_id;
  std::optional<std::string> origin;
  std::unordered_map<std::string, std::string> trace_tags;
  std::optional<std::string> trace_tags_extraction_error;
  std::optional<SamplingDecision> sampling_decision;
  // TODO: Whether the requester delegated its sampling decision will also be
  // important eventually.

  DatadogExtractionPolicy extract;

  auto maybe_trace_id = extract.trace_id(reader);
  if (auto* error = std::get_if<Error>(&maybe_trace_id)) {
    return std::move(*error);
  }
  trace_id = std::get<0>(maybe_trace_id);

  origin = extract.origin(reader);

  auto maybe_parent_id = extract.parent_id(reader);
  if (auto* error = std::get_if<Error>(&maybe_parent_id)) {
    return std::move(*error);
  }
  parent_id = std::get<0>(maybe_parent_id);

  // Some information might be missing.
  // Here are the combinations considered:
  //
  // - no trace ID and no parent ID
  //     - this means there's no span to extract
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
  if (!parent_id && !origin) {
    return Error{Error::MISSING_PARENT_SPAN_ID,
                 "There's no parent span ID to extract."};
  }

  if (!parent_id) {
    // We have a trace ID, but not parent ID.  We're meant to be the root, and
    // whoever called us already created a trace ID for us (to correlate with
    // whatever they're doing).
    parent_id = 0;
  }

  auto maybe_sampling_priority = extract.sampling_priority(reader);
  if (auto* error = std::get_if<Error>(&maybe_sampling_priority)) {
    return std::move(*error);
  }
  auto sampling_priority = std::get<0>(maybe_sampling_priority);
  if (sampling_priority) {
    SamplingDecision decision;
    decision.priority = *sampling_priority;
    // `decision.mechanism` is null.  We might be able to infer it once we
    // extract `trace_tags`, but we would have no use for it, so we won't.
    decision.origin = SamplingDecision::Origin::EXTRACTED;

    sampling_decision = decision;
  }

  auto maybe_trace_tags = extract.trace_tags(reader);
  if (auto* error = std::get_if<Error>(&maybe_trace_tags)) {
    // Failure to parse trace tags is tolerated, with a diagnostic.
    // TODO: need a logger
    std::cout << *error << '\n';
    if (error->code == Error::TRACE_TAGS_EXCEED_MAXIMUM_LENGTH) {
      trace_tags_extraction_error = "extract_max_size";
    } else {
      trace_tags_extraction_error = "decoding_error";
    }
  } else {
    trace_tags = std::get<0>(maybe_trace_tags);
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
  if (trace_tags_extraction_error) {
    span_data->tags[tags::internal::propagation_error] =
        *trace_tags_extraction_error;
  }

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      collector_, trace_sampler_, span_sampler_, defaults_, injection_styles_,
      hostname_, origin, trace_tags, sampling_decision, std::move(span_data));
  Span span{span_data_ptr, segment, generator_.generate_span_id, clock_};
  return span;
}

}  // namespace tracing
}  // namespace datadog
