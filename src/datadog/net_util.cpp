#include "net_util.h"

#ifdef _MSC_VER
#include <winsock.h>
#else
#include <unistd.h>
#endif

namespace datadog {
namespace tracing {

std::optional<std::string> get_hostname() {
  char buffer[256];
  if (::gethostname(buffer, sizeof buffer)) {
    return std::nullopt;
  }
  return buffer;
}

}  // namespace tracing
}  // namespace datadog
