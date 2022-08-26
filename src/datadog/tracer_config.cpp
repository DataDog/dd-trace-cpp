#include "tracer_config.h"

#include "cerr_logger.h"
#include "datadog_agent.h"
#include "span_sampler.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig& config) {
  // TODO: environment variables, validation, and other fun.
  FinalizedTracerConfig result;

  if (config.defaults.service.empty()) {
    return Error{Error::SERVICE_NAME_REQUIRED, "Service name is required."};
  }

  result.defaults = config.defaults;

  if (config.logger) {
    result.logger = config.logger;
  } else {
    result.logger = std::make_shared<CerrLogger>();
  }

  if (!config.collector) {
    auto finalized = finalize_config(config.agent);
    if (auto* error = finalized.if_error()) {
      return std::move(*error);
    }
    result.collector =
        std::make_shared<DatadogAgent>(*finalized, result.logger);
  }

  if (auto trace_sampler_config = finalize_config(config.trace_sampler)) {
    result.trace_sampler =
        std::make_shared<TraceSampler>(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config = finalize_config(config.span_sampler)) {
    result.span_sampler = std::make_shared<SpanSampler>(*span_sampler_config);
  } else {
    return std::move(span_sampler_config.error());
  }

  // TODO: implement the other styles
  const auto not_implemented = [](std::string_view style,
                                  std::string_view operation) {
    std::string message;
    message += "The ";
    message += style;
    message += ' ';
    message += operation;
    message += " style is not yet supported. Only datadog is supported.";
    return Error{Error::NOT_IMPLEMENTED, std::move(message)};
  };

  if (config.extraction_styles.b3) {
    return not_implemented("b3", "extraction");
  } else if (config.extraction_styles.w3c) {
    return not_implemented("w3c", "extraction");
  } else if (config.injection_styles.b3) {
    return not_implemented("b3", "injection");
  } else if (config.injection_styles.w3c) {
    return not_implemented("w3c", "injection");
  } else if (!config.extraction_styles.datadog) {
    return Error{Error::MISSING_SPAN_EXTRACTION_STYLE,
                 "At least one extraction style must be specified."};
  } else if (!config.injection_styles.datadog) {
    return Error{Error::MISSING_SPAN_INJECTION_STYLE,
                 "At least one injection style must be specified."};
  }

  result.injection_styles = config.injection_styles;
  result.extraction_styles = config.extraction_styles;

  result.report_hostname = config.report_hostname;

  return result;
}

}  // namespace tracing
}  // namespace datadog
