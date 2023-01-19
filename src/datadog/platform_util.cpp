#include "platform_util.h"

#ifdef _MSC_VER
#include <winsock.h>
#else
#include <unistd.h>
#endif

namespace datadog {
namespace tracing {

Optional<std::string> get_hostname() {
  char buffer[256];
  if (::gethostname(buffer, sizeof buffer)) {
    return nullopt;
  }
  return buffer;
}

}  // namespace tracing
}  // namespace datadog
