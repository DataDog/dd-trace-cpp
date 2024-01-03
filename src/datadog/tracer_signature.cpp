#include "tracer_signature.h"

#include "version.h"

namespace datadog {
namespace tracing {

TracerSignature::TracerSignature(const RuntimeID& runtime_id,
                                 const std::string& default_service,
                                 const std::string& default_environment)
    : runtime_id_(runtime_id),
      default_service_(default_service),
      default_environment_(default_environment) {}

const RuntimeID& TracerSignature::runtime_id() const { return runtime_id_; }

StringView TracerSignature::default_service() const { return default_service_; }

StringView TracerSignature::default_environment() const {
  return default_environment_;
}

StringView TracerSignature::library_version() const { return tracer_version; }

StringView TracerSignature::library_language() const { return "cpp"; }

StringView TracerSignature::library_language_version() const {
  static const std::string value = std::to_string(__cplusplus);
  return value;
}

}  // namespace tracing
}  // namespace datadog
