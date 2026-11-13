#include <datadog/optional.h>
#include <datadog/telemetry/telemetry.h>

#include "telemetry_impl.h"

namespace datadog::telemetry {

namespace details {
// Generic overload function (P0051R3) like implementation.
template <typename... Ts>
struct Overload : Ts... {
  using Ts::operator()...;
};

// Guide required for C++17. Remove once we switch to C++20.
template <class... Ts>
Overload(Ts...) -> Overload<Ts...>;
}  // namespace details

// `TelemetryProxy` holds either the real implementation or a no-op
// implementation.
using NoopTelemetry = std::monostate;
using TelemetryProxy = std::variant<NoopTelemetry, std::shared_ptr<Telemetry>>;

struct TelemetryParameters final {
  FinalizedConfiguration configuration;
  tracing::TracerSignature tracer_signature;
  std::shared_ptr<tracing::Logger> logger;
  std::shared_ptr<tracing::HTTPClient> client;
  std::shared_ptr<tracing::EventScheduler> scheduler;
  tracing::HTTPClient::URL agent_url;
  tracing::Clock clock = tracing::default_clock;
};

TelemetryProxy make_telemetry(
    const tracing::Optional<TelemetryParameters>& init) {
  if (!init || !init->configuration.enabled) return NoopTelemetry{};
  return Telemetry::create(init->configuration, init->tracer_signature,
                           init->logger, init->client, init->scheduler,
                           init->agent_url, init->clock);
}

TelemetryProxy& instance(
    const tracing::Optional<TelemetryParameters>& init = tracing::nullopt) {
  // This singleton is never destroyed on purpose because the Tracer's
  // destructor needs it.
  static TelemetryProxy& telemetry = *new TelemetryProxy{make_telemetry(init)};

  // Send the app-closing message when statics are destroyed.
  struct ShutdownTelemetryAtExit {
    ~ShutdownTelemetryAtExit() { shutdown(); }
  };
  static const ShutdownTelemetryAtExit shutdown_telemetry_at_exit;

  return telemetry;
}

void init(FinalizedConfiguration configuration,
          tracing::TracerSignature tracer_signature,
          std::shared_ptr<tracing::Logger> logger,
          std::shared_ptr<tracing::HTTPClient> client,
          std::shared_ptr<tracing::EventScheduler> event_scheduler,
          tracing::HTTPClient::URL agent_url, tracing::Clock clock) {
  instance(TelemetryParameters{configuration, tracer_signature, logger, client,
                               event_scheduler, agent_url, clock});
}

void init(FinalizedConfiguration configuration,
          std::shared_ptr<tracing::Logger> logger,
          std::shared_ptr<tracing::HTTPClient> client,
          std::shared_ptr<tracing::EventScheduler> event_scheduler,
          tracing::HTTPClient::URL agent_url, tracing::Clock clock) {
  init(configuration,
       tracing::TracerSignature(tracing::RuntimeID::generate(),
                                tracing::get_process_name(), ""),
       logger, client, event_scheduler, agent_url, clock);
}

void shutdown() {
  std::visit(
      details::Overload{
          [](std::shared_ptr<Telemetry>& telemetry) { telemetry->shutdown(); },
          [](NoopTelemetry) {},
      },
      instance());
}

void send_configuration_change() {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->send_configuration_change();
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void capture_configuration_change(
    const std::vector<tracing::ConfigMetadata>& new_configuration) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->capture_configuration_change(new_configuration);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void capture_product_change(const Product& product) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->capture_product_change(product);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

namespace log {
void warning(std::string message) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->log_warning(message);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void error(std::string message) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->log_error(message);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void error(std::string message, std::string stacktrace) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->log_error(message, stacktrace);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}
}  // namespace log

namespace counter {
void increment(const Counter& counter) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->increment_counter(counter);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void increment(const Counter& counter, const std::vector<std::string>& tags) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->increment_counter(counter, tags);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void decrement(const Counter& counter) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->decrement_counter(counter);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void decrement(const Counter& counter, const std::vector<std::string>& tags) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->decrement_counter(counter, tags);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void set(const Counter& counter, uint64_t value) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->set_counter(counter, value);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void set(const Counter& counter, const std::vector<std::string>& tags,
         uint64_t value) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->set_counter(counter, tags, value);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

}  // namespace counter

namespace rate {
void set(const Rate& rate, uint64_t value) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->set_rate(rate, value);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void set(const Rate& rate, const std::vector<std::string>& tags,
         uint64_t value) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->set_rate(rate, tags, value);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}
}  // namespace rate

namespace distribution {

void add(const Distribution& distribution, uint64_t value) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->add_datapoint(distribution, value);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

void add(const Distribution& distribution, const std::vector<std::string>& tags,
         uint64_t value) {
  std::visit(details::Overload{
                 [&](std::shared_ptr<Telemetry>& telemetry) {
                   telemetry->add_datapoint(distribution, tags, value);
                 },
                 [](NoopTelemetry) {},
             },
             instance());
}

}  // namespace distribution

}  // namespace datadog::telemetry
