#include "datadog/c/tracer.h"

#include <datadog/collector.h>
#include <datadog/hex.h>
#include <datadog/msgpack.h>
#include <datadog/span_data.h>
#include <datadog/trace_segment.h>
#include <datadog/tracer.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace dd = datadog::tracing;

namespace {

class ContextReader : public dd::DictReader {
  dd_context_read_callback read_;

 public:
  explicit ContextReader(dd_context_read_callback read_callback)
      : read_(read_callback) {}

  dd::Optional<dd::StringView> lookup(dd::StringView key) const override {
    std::string key_str(key);
    if (auto value = read_(key_str.c_str())) {
      return value;
    }
    return dd::nullopt;
  }

  void visit(const std::function<void(dd::StringView key, dd::StringView value)>
                 & /* visitor */) const override {}
};

class ContextWriter : public dd::DictWriter {
  dd_context_write_callback write_;

 public:
  explicit ContextWriter(dd_context_write_callback func) : write_(func) {}

  void set(dd::StringView key, dd::StringView value) override {
    std::string key_str(key);
    std::string value_str(value);
    write_(key_str.c_str(), value_str.c_str());
  }
};

// Collector that forwards finished trace chunks to a C callback. The payload
// matches the /v0.4/traces HTTP body: a 1-element outer array containing
// the chunk, produced by the same msgpack encoder the DatadogAgent uses.
class CallbackCollector : public dd::Collector {
  dd_trace_msgpack_callback callback_;
  void *user_data_;

 public:
  CallbackCollector(dd_trace_msgpack_callback callback, void *user_data)
      : callback_(callback), user_data_(user_data) {}

  dd::Expected<void> send(
      std::vector<std::unique_ptr<dd::SpanData>> &&spans,
      const std::shared_ptr<dd::TraceSampler> & /*response_handler*/) override {
    std::string buffer;
    if (auto rc = dd::msgpack::pack_array(buffer, std::size_t{1}); !rc) {
      return rc;
    }
    if (auto rc = dd::msgpack_encode(buffer, spans); !rc) {
      return rc;
    }
    callback_(reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size(),
              user_data_);
    return {};
  }

  std::string config() const override {
    return "{\"type\":\"datadog::binding::c::CallbackCollector\"}";
  }
};

dd::SpanConfig make_span_config(dd_span_options_t options) {
  dd::SpanConfig span_config;
  if (options.name != nullptr) {
    span_config.name = options.name;
  }
  if (options.resource != nullptr) {
    span_config.resource = options.resource;
  }
  if (options.service != nullptr) {
    span_config.service = options.service;
  }
  if (options.service_type != nullptr) {
    span_config.service_type = options.service_type;
  }
  if (options.environment != nullptr) {
    span_config.environment = options.environment;
  }
  if (options.version != nullptr) {
    span_config.version = options.version;
  }
  return span_config;
}

void set_error(dd_error_t *error, dd_error_code code, const char *message) {
  if (error == nullptr) {
    return;
  }
  error->code = code;
  std::strncpy(error->message, message, sizeof(error->message) - 1);
  error->message[sizeof(error->message) - 1] = '\0';
}

}  // namespace

extern "C" {

dd_conf_t *dd_tracer_conf_new(void) {
  try {
    return reinterpret_cast<dd_conf_t *>(new dd::TracerConfig);
  } catch (...) {
    return nullptr;
  }
}

void dd_tracer_conf_free(dd_conf_t *handle) {
  if (handle == nullptr) {
    return;
  }
  delete reinterpret_cast<dd::TracerConfig *>(handle);
}

void dd_tracer_conf_set(dd_conf_t *handle, dd_tracer_option option,
                        const void *value) {
  if (handle == nullptr || value == nullptr) {
    return;
  }

  auto *cfg = reinterpret_cast<dd::TracerConfig *>(handle);

  switch (option) {
    case DD_OPT_SERVICE_NAME:
      cfg->service = static_cast<const char *>(value);
      break;
    case DD_OPT_ENV:
      cfg->environment = static_cast<const char *>(value);
      break;
    case DD_OPT_VERSION:
      cfg->version = static_cast<const char *>(value);
      break;
    case DD_OPT_AGENT_URL:
      cfg->agent.url = static_cast<const char *>(value);
      break;
    case DD_OPT_INTEGRATION_NAME:
      cfg->integration_name = static_cast<const char *>(value);
      break;
    case DD_OPT_INTEGRATION_VERSION:
      cfg->integration_version = static_cast<const char *>(value);
      break;
  }
}

void dd_tracer_conf_set_collector_callback(dd_conf_t *handle,
                                           dd_trace_msgpack_callback callback,
                                           void *user_data) {
  if (handle == nullptr) {
    return;
  }
  auto *cfg = reinterpret_cast<dd::TracerConfig *>(handle);
  if (callback == nullptr) {
    cfg->collector.reset();
    return;
  }
  cfg->collector = std::make_shared<CallbackCollector>(callback, user_data);
  // Telemetry is silenced unconditionally in dd_tracer_new after
  // finalize_config, because env vars like DD_INSTRUMENTATION_TELEMETRY_ENABLED
  // outrank user-config fields set here. Remote-config polling is implicitly
  // disabled: when a custom collector is present, the Tracer skips DatadogAgent
  // construction entirely, so no RC loop starts. The agent URL is left alone:
  // callback mode never contacts it, but clobbering it here would silently
  // lose a user-set URL across a set/clear callback cycle.
}

dd_tracer_t *dd_tracer_new(const dd_conf_t *conf_handle, dd_error_t *error) {
  if (conf_handle == nullptr) {
    set_error(error, DD_ERROR_NULL_ARGUMENT, "conf_handle is NULL");
    return nullptr;
  }

  const auto *config = reinterpret_cast<const dd::TracerConfig *>(conf_handle);
  auto validated_config = dd::finalize_config(*config);
  if (!validated_config) {
    set_error(error, DD_ERROR_INVALID_CONFIG,
              validated_config.error().message.c_str());
    return nullptr;
  }

  // When a custom collector is installed (only possible via
  // dd_tracer_conf_set_collector_callback), force telemetry off. We must do
  // this after finalize_config because env vars like
  // DD_INSTRUMENTATION_TELEMETRY_ENABLED outrank user-config fields and would
  // otherwise keep telemetry network traffic alive despite the callback.
  if (std::holds_alternative<std::shared_ptr<dd::Collector>>(
          validated_config->collector)) {
    validated_config->telemetry.enabled = false;
    validated_config->telemetry.report_metrics = false;
    validated_config->telemetry.report_logs = false;
  }

  try {
    return reinterpret_cast<dd_tracer_t *>(new dd::Tracer{*validated_config});
  } catch (...) {
    set_error(error, DD_ERROR_ALLOCATION_FAILURE, "failed to allocate tracer");
    return nullptr;
  }
}

void dd_tracer_free(dd_tracer_t *tracer_handle) {
  if (tracer_handle == nullptr) {
    return;
  }
  delete reinterpret_cast<dd::Tracer *>(tracer_handle);
}

dd_span_t *dd_tracer_create_span(dd_tracer_t *tracer_handle,
                                 dd_span_options_t options) {
  if (tracer_handle == nullptr || options.name == nullptr) {
    return nullptr;
  }

  auto *tracer = reinterpret_cast<dd::Tracer *>(tracer_handle);
  auto span_config = make_span_config(options);

  try {
    return reinterpret_cast<dd_span_t *>(
        new dd::Span(tracer->create_span(span_config)));
  } catch (...) {
    return nullptr;
  }
}

dd_span_t *dd_tracer_extract_or_create_span(
    dd_tracer_t *tracer_handle, dd_context_read_callback on_context_read,
    dd_span_options_t options) {
  if (tracer_handle == nullptr || on_context_read == nullptr ||
      options.name == nullptr) {
    return nullptr;
  }

  auto *tracer = reinterpret_cast<dd::Tracer *>(tracer_handle);
  auto span_config = make_span_config(options);

  ContextReader reader(on_context_read);
  try {
    return reinterpret_cast<dd_span_t *>(
        new dd::Span(tracer->extract_or_create_span(reader, span_config)));
  } catch (...) {
    return nullptr;
  }
}

void dd_span_free(dd_span_t *span_handle) {
  if (span_handle == nullptr) {
    return;
  }
  delete reinterpret_cast<dd::Span *>(span_handle);
}

void dd_span_set_tag(dd_span_t *span_handle, const char *key,
                     const char *value) {
  if (span_handle == nullptr || key == nullptr || value == nullptr) {
    return;
  }
  reinterpret_cast<dd::Span *>(span_handle)->set_tag(key, value);
}

void dd_span_set_error(dd_span_t *span_handle, int error_value) {
  if (span_handle == nullptr) {
    return;
  }
  reinterpret_cast<dd::Span *>(span_handle)->set_error(error_value != 0);
}

void dd_span_set_error_message(dd_span_t *span_handle,
                               const char *error_message) {
  if (span_handle == nullptr || error_message == nullptr) {
    return;
  }
  reinterpret_cast<dd::Span *>(span_handle)->set_error_message(error_message);
}

void dd_span_inject(dd_span_t *span_handle,
                    dd_context_write_callback on_context_write) {
  if (span_handle == nullptr || on_context_write == nullptr) {
    return;
  }

  auto *span = reinterpret_cast<dd::Span *>(span_handle);
  ContextWriter writer(on_context_write);
  span->inject(writer);
}

dd_span_t *dd_span_create_child(dd_span_t *span_handle,
                                dd_span_options_t options) {
  if (span_handle == nullptr || options.name == nullptr) {
    return nullptr;
  }

  auto *span = reinterpret_cast<dd::Span *>(span_handle);
  auto span_config = make_span_config(options);

  try {
    return reinterpret_cast<dd_span_t *>(
        new dd::Span(span->create_child(span_config)));
  } catch (...) {
    return nullptr;
  }
}

void dd_span_finish(dd_span_t *span_handle) {
  if (span_handle == nullptr) {
    return;
  }
  reinterpret_cast<dd::Span *>(span_handle)
      ->set_end_time(std::chrono::steady_clock::now());
}

int dd_span_get_trace_id(dd_span_t *span_handle, char *buffer,
                         size_t buffer_size) {
  if (span_handle == nullptr || buffer == nullptr || buffer_size == 0) {
    return -1;
  }

  auto *span = reinterpret_cast<dd::Span *>(span_handle);
  std::string hex = span->trace_id().hex_padded();

  if (hex.size() >= buffer_size) {
    return -1;
  }

  std::strncpy(buffer, hex.c_str(), buffer_size);
  // Safe narrowing: hex trace IDs are at most 32 characters.
  return static_cast<int>(hex.size());
}

int dd_span_get_span_id(dd_span_t *span_handle, char *buffer,
                        size_t buffer_size) {
  if (span_handle == nullptr || buffer == nullptr || buffer_size == 0) {
    return -1;
  }

  auto *span = reinterpret_cast<dd::Span *>(span_handle);
  std::string hex = dd::hex_padded(span->id());

  if (hex.size() >= buffer_size) {
    return -1;
  }

  std::strncpy(buffer, hex.c_str(), buffer_size);
  // Safe narrowing: hex span IDs are 16 characters.
  return static_cast<int>(hex.size());
}

void dd_span_set_resource(dd_span_t *span_handle, const char *resource) {
  if (span_handle == nullptr || resource == nullptr) {
    return;
  }
  reinterpret_cast<dd::Span *>(span_handle)->set_resource_name(resource);
}

void dd_span_set_service(dd_span_t *span_handle, const char *service) {
  if (span_handle == nullptr || service == nullptr) {
    return;
  }
  reinterpret_cast<dd::Span *>(span_handle)->set_service_name(service);
}

}  // extern "C"
