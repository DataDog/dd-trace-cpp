#pragma once

#include "batch.h"

namespace datadog::telemetry {

template <typename T>
class Serializer {
 public:
  void operator()(Batch batch) { static_cast<T*>(this)(batch); }

  const char* get_buffer() const { return static_cast<T*>(this)->get_buffer(); }
};

}  // namespace datadog::telemetry
