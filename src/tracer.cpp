#include "tracer.h"

#include "datadog_agent.h"
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

std::variant<std::uint64_t, Error> parse_uint64(std::string_view input,
                                                int base) {
  std::uint64_t value;
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
    message += "Integer is not within the range of 64-bit unsigned: ";
    message += input;
    return Error{Error::OUT_OF_RANGE_OF_UINT64, std::move(message)};
  }
  return value;
}

struct ExtractionPolicy {
  virtual std::variant<std::optional<std::uint64_t>, Error> trace_id(
      const DictReader&) = 0;
  virtual std::variant<std::optional<std::uint64_t>, Error> parent_id(
      const DictReader&) = 0;
  virtual std::variant<std::optional<int>, Error> sampling_priority(
      const DictReader&) = 0;
  virtual std::variant<std::optional<std::string>, Error> origin(
      const DictReader&) = 0;
  virtual std::variant<std::unordered_map<std::string, std::string>, Error>
  trace_tags(const DictReader&) = 0;
};

struct DatadogExtractionPolicy : public ExtractionPolicy {
  std::variant<std::optional<std::uint64_t>, Error> trace_id(
      const DictReader&) override;
  std::variant<std::optional<std::uint64_t>, Error> parent_id(
      const DictReader&) override;
  std::variant<std::optional<int>, Error> sampling_priority(
      const DictReader&) override;
  std::variant<std::optional<std::string>, Error> origin(
      const DictReader&) override;
  std::variant<std::unordered_map<std::string, std::string>, Error> trace_tags(
      const DictReader&) override;
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

  // TODO: ExtractionPolicy
  (void)reader;
  (void)config;
}

}  // namespace tracing
}  // namespace datadog
