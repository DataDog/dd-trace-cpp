#include <datadog/expected.h>
#include <datadog/http_client.h>

#include <memory>

namespace datadog::telemetry {

enum class site : char { us1, us3, us5, ap1, eu };

class IntakeExporter final {
  std::string api_key_;
  tracing::HTTPClient::URL intake_url_;
  std::shared_ptr<tracing::HTTPClient> client_;

 public:
  IntakeExporter(std::shared_ptr<tracing::HTTPClient> client,
                 std::string api_key, site datacenter = site::us1);

  tracing::Expected<void> send(std::string request_type, std::string payload);
};

class AgentExporter final {
  tracing::HTTPClient::URL telemetry_endpoint_;
  std::shared_ptr<tracing::HTTPClient> client_;

 public:
  AgentExporter(std::shared_ptr<tracing::HTTPClient> client,
                tracing::HTTPClient::URL agent_url);

  tracing::Expected<void> send(std::string request_type, std::string payload);
};

}  // namespace datadog::telemetry
