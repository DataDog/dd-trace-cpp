#pragma once

// This component provides a class, `TracerSignature`, that contains the parts
// of a tracer's configuration that are used to refer to the tracer in Datadog's
// telemetry and remote configuration APIs.
//
// `TracerSignature` is used in three contexts:
//
// 1. When telemetry is sent to the Datadog Agent, the tracer signature is
//    included in the request payload. See
//    `TracerTelemetry::generate_telemetry_body` in `tracer_telemetry.cpp`.
// 2. When the Datadog Agent is polled for configuration updates, part of the
//    tracer signature (all but the language version) is included in the request
//    payload. See `RemoteConfigurationManager::make_request_payload` in
//    `remote_config.h`.
// 3. When the Datadog Agent responds with configuration updates, the service
//    and environment of the tracer signature are used to determine whether the
//    updates are relevant to the `Tracer` that created the collector that is
//    polling the Datadog Agent. See
//    `RemoteConfigurationManager::process_response` in `remote_config.h`.

#include <string>

#include "runtime_id.h"
#include "string_view.h"

namespace datadog {
namespace tracing {

class TracerSignature {
  RuntimeID runtime_id_;
  std::string default_service_;
  std::string default_environment_;

 public:
  // Create a tracer signature having the specified `runtime_id`,
  // `default_service`, and `default_environment`. `default_service` and
  // `default_environment` refer to the corresponding fields from
  // `SpanDefaults`.
  TracerSignature(const RuntimeID& runtime_id,
                  const std::string& default_service,
                  const std::string& default_environment);

  // Return the runtime ID with which the tracer was configured.
  const RuntimeID& runtime_id() const;
  // Return the `SpanDefaults::service` with which the tracer was configured.
  StringView default_service() const;
  // Return the `SpanDefaults::environment` with which the tracer was
  // configured.
  StringView default_environment() const;
  // Return the version of this tracing library (`tracer_version` from
  // `version.h`).
  StringView library_version() const;
  // Return the name of the programming language in which this library is
  // written: "cpp".
  StringView library_language() const;
  // Return the version of C++ standard used to compile this library. It should
  // be "201703".
  StringView library_language_version() const;
};

}  // namespace tracing
}  // namespace datadog
