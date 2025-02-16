#include "telemetry/exporter.h"

#include <datadog/clock.h>
#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>

namespace datadog::telemetry {
namespace {

constexpr std::string_view intake_url(site datacenter) {
  switch (datacenter) {
    case site::us1:
      return "";
      break;
    case site::us3:
      return "";
      break;
    case site::us5:
      return "";
      break;
    case site::ap1:
      return "";
      break;
    case site::eu:
      return "";
      break;
  }
};

}  // namespace

IntakeExporter::IntakeExporter(std::shared_ptr<tracing::HTTPClient> client,
                               std::string api_key, site datacenter)
    : api_key_(std::move(api_key)),
      intake_url_(*tracing::HTTPClient::URL::parse(intake_url(datacenter))),
      client_(std::move(client)) {}

tracing::Expected<void> IntakeExporter::send(std::string request_type,
                                             std::string payload) {
  auto header_setter = [this, &payload,
                        &request_type](tracing::DictWriter& headers) {
    headers.set("Content-Type", "application/json");
    headers.set("Content-Length", std::to_string(payload.size()));
    headers.set("DD-Telemetry-API-Version", "v2");
    headers.set("DD-Client-Library-Language", "cpp");
    /*headers.set("DD-Client-Library-Version",
                tracer_signature->library_version);*/
    headers.set("DD-Telemetry-Request-Type", request_type);
    headers.set("DD-API-KEY", api_key_);
  };
  auto on_error = [](tracing::Error) {};
  auto on_response = [](int status, const tracing::DictReader& headers,
                        std::string body) {
    (void)status;
    (void)headers;
    (void)body;
  };

  return client_->post(
      intake_url_, header_setter, payload, on_response, on_error,
      tracing::default_clock().tick + std::chrono::seconds(20));
}

AgentExporter::AgentExporter(std::shared_ptr<tracing::HTTPClient> client,
                             tracing::HTTPClient::URL agent_url)
    : client_(std::move(client)) {
  telemetry_endpoint_ = std::move(agent_url);
  telemetry_endpoint_.path += "/telemetry/proxy/api/v2/apmtelemetry";
}

tracing::Expected<void> send(std::string, std::string) { return {}; }

};  // namespace datadog::telemetry
