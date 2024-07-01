#include <datadog/logger.h>
#include <datadog/remote_config/listener.h>
#include <datadog/remote_config/remote_config.h>
#include <datadog/tracer_signature.h>

#include <datadog/json.hpp>

class NullLogger : public datadog::tracing::Logger {
 public:
  ~NullLogger() override = default;

  void log_error(const LogFunc&) override{};
  void log_startup(const LogFunc&) override{};
};

// TODO: Add section for APM_TRACING using real config manager
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, size_t size) {
  datadog::tracing::TracerSignature tracer_sig(
      datadog::tracing::RuntimeID::generate(), "fuzz-remote-configuration",
      "test");
  auto logger = std::make_shared<NullLogger>();
  std::vector<std::shared_ptr<datadog::remote_config::Listener>> listeners{};
  datadog::remote_config::Manager manager(tracer_sig, listeners, logger);

  const nlohmann::json j = std::string(data, size);
  manager.process_response(j);
  auto request_payload = make_request_payload();
  (void)request_payload;

  return 0;
}
