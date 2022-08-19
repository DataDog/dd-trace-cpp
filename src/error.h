#pragma once

#include <string>

namespace datadog {
namespace tracing {

struct Error {
  int code;
  std::string message;

  enum {
    SERVICE_NAME_REQUIRED = 1,
    MESSAGEPACK_ENCODE_FAILURE = 2,
    CURL_REQUEST_FAILURE = 3,
    YOU_DID_A_BAD_THING,
    UH_OH_NOT_AGAIN,
    DO_YOU_EVEN_KNOW_WHAT_SHAME_IS
  };
};

}  // namespace tracing
}  // namespace datadog
