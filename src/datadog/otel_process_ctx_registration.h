#pragma once

#include <datadog/optional.h>

#include <memory>
#include <string>
#include <utility>

namespace datadog::tracing {

class Logger;

struct OtelCtxFields {
  std::string service_env;
  std::string service_name;
  std::string service_version;
  std::string tracer_language;
  std::string tracer_version;
  Optional<std::string> hostname;
  std::string container_id;
  std::string process_tags;

  bool operator==(const OtelCtxFields& other) const {
    return service_env == other.service_env &&
           service_name == other.service_name &&
           service_version == other.service_version &&
           tracer_language == other.tracer_language &&
           tracer_version == other.tracer_version &&
           hostname == other.hostname && container_id == other.container_id &&
           process_tags == other.process_tags;
  }
};

// RAII handle representing one Tracer's contribution to the published
// OpenTelemetry process context. Destroying it withdraws that contribution.
//
// The OTel context is a per-process singleton, but several Tracers can be alive
// at once in a single process. For instance, the Envoy integration creates one
// Tracer per worker thread.
// We validate that config fields stay the same between all tracer instances,
// which is how the known integrations behave today -- the only field that's
// expected to differ is the `runtime_id`.
//
// The published context is reference-counted across all live tracer instances:
//
//   * While exactly one registration is alive, the context is published in
//     full, including its `runtime_id`
//   * While two or more registrations are alive, the `runtime_id` is
//     omitted, since this field is expected to differ between tracer instances.
//
// This class is thread-safe (takes care of protecting any global state).
class OtelCtxRegistration {
 public:
  // Publish the OTel process context built from `fields`, for a tracer with the
  // given `runtime_id` (its `service_instance_id`). Returns a registration
  // whose destruction updates or drops the process-wide context. This is
  // nullptr outside of Linux or on failure.
  static std::unique_ptr<OtelCtxRegistration> publish(
      const OtelCtxFields& fields, const std::string& runtime_id,
      Logger& logger);

  ~OtelCtxRegistration();

  OtelCtxRegistration(const OtelCtxRegistration&) = delete;
  OtelCtxRegistration& operator=(const OtelCtxRegistration&) = delete;
  OtelCtxRegistration(OtelCtxRegistration&&) = delete;
  OtelCtxRegistration& operator=(OtelCtxRegistration&&) = delete;

 private:
  explicit OtelCtxRegistration(std::string runtime_id)
      : runtime_id_(std::move(runtime_id)) {}

  std::string runtime_id_;
};

}  // namespace datadog::tracing
