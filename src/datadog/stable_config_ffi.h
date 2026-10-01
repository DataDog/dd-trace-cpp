#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dd_trace_stable_config_result dd_trace_stable_config_result;

typedef struct {
  const uint8_t* data;
  size_t len;
} dd_trace_stable_config_bytes;

typedef struct {
  dd_trace_stable_config_bytes name;
  dd_trace_stable_config_bytes value;
  dd_trace_stable_config_bytes config_id;
  uint32_t source;  // 0 is local, 1 is fleet.
} dd_trace_stable_config_entry_view;

dd_trace_stable_config_result* dd_trace_stable_config_load(
    dd_trace_stable_config_bytes language,
    dd_trace_stable_config_bytes local_path,
    dd_trace_stable_config_bytes fleet_path);
size_t dd_trace_stable_config_count(
    const dd_trace_stable_config_result* result);
bool dd_trace_stable_config_entry(const dd_trace_stable_config_result* result,
                                  size_t index,
                                  dd_trace_stable_config_entry_view* entry);
dd_trace_stable_config_bytes dd_trace_stable_config_error(
    const dd_trace_stable_config_result* result);
void dd_trace_stable_config_drop(dd_trace_stable_config_result* result);

#ifdef __cplusplus
}
#endif
