#include "id_generator.h"

#include <atomic>  // TODO: no

namespace datadog {
namespace tracing {

const IDGenerator default_id_generator = {
    // TODO: no
    []() {
      static std::atomic_uint64_t next_trace_id = 1;
      return next_trace_id++;
    },
    // TODO: no
    []() {
      static std::atomic_uint64_t next_span_id = 1001;
      return next_span_id++;
    },
};

}  // namespace tracing
}  // namespace datadog
