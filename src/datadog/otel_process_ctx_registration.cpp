#include "otel_process_ctx_registration.h"

#include <datadog/logger.h>
#include <datadog/version.h>

#include <mutex>
#include <ostream>
#include <set>
#include <vector>

#include "otel_process_ctx.h"

namespace datadog::tracing {
namespace {

// Process-wide OTel context state, including the mutex that guards it.
// Accessed through the single `get_otel_ctx_state()` instance.
struct StaticOtelCtxState {
  std::mutex mutex;
  // Shared by all coexisting registrations. Present iff
  // `runtime_ids` is non-empty: established by the first live registration and
  // dropped once the last one is released.
  Optional<OtelCtxFields> common;
  // The `service_instance_id` (runtime id) of each live registration. This is a
  // multiset because several tracers may share one runtime id and each of them
  // counts separately.
  std::multiset<std::string> runtime_ids;
};

StaticOtelCtxState& get_otel_ctx_state() {
  static StaticOtelCtxState state;
  return state;
}

// Removes one registration for `runtime_id` from `state`. Must be called with
// `state.mutex` held.
void unregister(StaticOtelCtxState& state, const std::string& runtime_id) {
  // Erase only one runtime id, leaving copies, if any
  const std::multiset<std::string>::iterator entry =
      state.runtime_ids.find(runtime_id);
  if (entry != state.runtime_ids.end()) {
    state.runtime_ids.erase(entry);
  }
  if (state.runtime_ids.empty()) {
    state.common.reset();
  }
}

// (Re)publishes or drops the process context to reflect `state`. Must be called
// with `state.mutex` held.
otel_process_ctx_result upsert(const StaticOtelCtxState& state) {
  const std::multiset<std::string>& runtime_ids = state.runtime_ids;
  if (runtime_ids.empty()) {
    otel_process_ctx_drop_current();
    return {true, nullptr};
  }

  const OtelCtxFields& common = *state.common;
  // While all live registrations report one runtime id, that id represents the
  // process and we publish it; once ids differ, none does, so it is omitted
  // (see class docs). The multiset is sorted, so the first and last entries are
  // equal exactly when every entry is.
  const bool include_instance_id =
      *runtime_ids.begin() == *runtime_ids.rbegin();

  std::vector<const char*> resource_attrs;
  if (common.hostname) {
    resource_attrs = {"host.name", common.hostname->c_str(), "container.id",
                      common.container_id.c_str(), nullptr};
  } else {
    resource_attrs = {"container.id", common.container_id.c_str(), nullptr};
  }
  std::vector<const char*> extra_attrs = {"datadog.process_tags",
                                          common.process_tags.c_str(), nullptr};

  otel_process_ctx_data data = {};
  data.deployment_environment_name = common.service_env.c_str();
  data.service_instance_id =
      include_instance_id ? runtime_ids.begin()->c_str() : "";
  data.service_name = common.service_name.c_str();
  data.service_version = common.service_version.c_str();
  data.telemetry_sdk_language = common.tracer_language.c_str();
  data.telemetry_sdk_version = common.tracer_version.c_str();
  data.telemetry_sdk_name = tracer_library_name;
  data.resource_attributes = resource_attrs.data();
  data.extra_attributes = extra_attrs.data();
  data.thread_ctx_config = nullptr;

  return otel_process_ctx_publish(&data);
}

}  // namespace

OtelCtxRegistration::~OtelCtxRegistration() {
  StaticOtelCtxState& state = get_otel_ctx_state();
  std::lock_guard<std::mutex> lock(state.mutex);
  unregister(state, runtime_id_);
  upsert(state);
}

#ifndef __linux__

std::unique_ptr<OtelCtxRegistration> OtelCtxRegistration::publish(
    const OtelCtxFields&, const std::string&, Logger&) {
  // Feature is Linux-only, nothing to do
  return nullptr;
}

#else

std::unique_ptr<OtelCtxRegistration> OtelCtxRegistration::publish(
    const OtelCtxFields& fields, const std::string& runtime_id,
    Logger& logger) {
  StaticOtelCtxState& state = get_otel_ctx_state();

  bool fields_differ = false;
  Optional<otel_process_ctx_result> publish_error;
  {
    std::lock_guard<std::mutex> lock(state.mutex);

    if (!state.common) {
      // First registration establishes the process-wide fields
      state.common = fields;
    } else if (!(*state.common == fields)) {
      fields_differ = true;
    }

    state.runtime_ids.insert(runtime_id);

    const otel_process_ctx_result result = upsert(state);
    if (!result.success) {
      unregister(state, runtime_id);
      publish_error = result;
    }
  }

  if (fields_differ) {
    logger.log_error([](std::ostream& log) {
      log << "OpenTelemetry process context fields differ between coexisting "
             "tracers; keeping the previously-published values";
    });
  }

  if (publish_error) {
    logger.log_error([&](std::ostream& log) {
      log << "Failed to publish OpenTelemetry process context: "
          << publish_error->error_message;
    });
    return nullptr;
  }

  return std::unique_ptr<OtelCtxRegistration>(
      new OtelCtxRegistration(runtime_id));
}

#endif

}  // namespace datadog::tracing
