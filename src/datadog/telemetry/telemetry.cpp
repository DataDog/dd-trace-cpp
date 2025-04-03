#include <datadog/optional.h>
#include <datadog/telemetry/telemetry.h>

#include "telemetry_impl.h"

namespace datadog::telemetry {
namespace details {

/// NOTE(@dmehala): Generic overload function (P0051R3) like implementation.
template <typename... Ts>
struct Overload : Ts... {
  using Ts::operator()...;
};

/// NOTE(@dmehala): Guide required for C++17. Remove once we switch to C++20.
template <class... Ts>
Overload(Ts...) -> Overload<Ts...>;

}  // namespace details

using NoopTelemetry = std::monostate;

/// `TelemetryProxy` holds either the real implementation or a no-op
/// implementation.
using TelemetryProxy = std::variant<NoopTelemetry, Telemetry>;

// NOTE(@dmehala): until metrics handling is improved.
static DefaultMetrics noop_metrics;

/// NOTE(@dmehala): Here to facilitate Meyer's singleton construction.
struct Ctor_param final {
  FinalizedConfiguration configuration;
  std::shared_ptr<tracing::Logger> logger;
  std::shared_ptr<tracing::HTTPClient> client;
  std::vector<std::shared_ptr<Metric>> metrics;
  std::shared_ptr<tracing::EventScheduler> scheduler;
  tracing::HTTPClient::URL agent_url;
  tracing::Clock clock = tracing::default_clock;
};

TelemetryProxy make_telemetry(const Ctor_param& init) {
  if (!init.configuration.enabled) return NoopTelemetry{};
  return Telemetry{init.configuration, init.logger,    init.client,
                   init.metrics,       init.scheduler, init.agent_url,
                   init.clock};
}

TelemetryProxy& instance(
    const tracing::Optional<Ctor_param>& init = tracing::nullopt) {
  static TelemetryProxy telemetry(make_telemetry(*init));
  return telemetry;
}

void init(FinalizedConfiguration configuration,
          std::shared_ptr<tracing::Logger> logger,
          std::shared_ptr<tracing::HTTPClient> client,
          std::vector<std::shared_ptr<Metric>> metrics,
          std::shared_ptr<tracing::EventScheduler> event_scheduler,
          tracing::HTTPClient::URL agent_url, tracing::Clock clock) {
  instance(Ctor_param{configuration, logger, client, metrics, event_scheduler,
                      agent_url, clock});
}

void send_app_started(const std::unordered_map<tracing::ConfigName,
                                               tracing::ConfigMetadata>& conf) {
  std::visit(
      details::Overload{
          [&](Telemetry& telemetry) { telemetry.send_app_started(conf); },
          [](NoopTelemetry) {},
      },
      instance());
}

void send_configuration_change() {
  std::visit(
      details::Overload{
          [&](Telemetry& telemetry) { telemetry.send_configuration_change(); },
          [](NoopTelemetry) {},
      },
      instance());
}

void capture_configuration_change(
    const std::vector<tracing::ConfigMetadata>& new_configuration) {
  std::visit(details::Overload{
                 [&](Telemetry& telemetry) {
                   telemetry.capture_configuration_change(new_configuration);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

DefaultMetrics& metrics() {
  auto& proxy = instance();
  if (std::holds_alternative<NoopTelemetry>(proxy)) {
    return noop_metrics;
  } else {
    return std::get<Telemetry>(proxy).metrics();
  }
}

void report_warning_log(std::string message) {
  std::visit(details::Overload{
                 [&](Telemetry& telemetry) { telemetry.log_warning(message); },
                 [](NoopTelemetry) {},
             },
             instance());
}

void report_error_log(std::string message) {
  std::visit(details::Overload{
                 [&](Telemetry& telemetry) { telemetry.log_error(message); },
                 [](NoopTelemetry) {},
             },
             instance());
}

}  // namespace datadog::telemetry
